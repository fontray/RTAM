#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <stdatomic.h>

#include "audio_hw.h"
#include "metrics.h"
#include "ring_buffer.h"

ALSA_Config alsa_config;
RingBuffer* rb_cap_to_change = NULL;  
RingBuffer* rb_change_to_play = NULL;
static atomic_int running = 1;

#define PB_SIZE (PERIOD_SIZE * CHANNELS)
#define RING_BUFFER_PERIODS 4
#define PROCESS_IDLE_SLEEP_NS 50000L
#define NS_PER_SEC 1000000000LL

void set_thread_affinity(pthread_t tid, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    int s = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);
    if(s != 0){
        perror("Thread affinity setting failed");
    }
    else{
        printf("Thread assign on %d\n", core_id);
    }
}

static void sleep_for_ns(long ns){
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = ns;
    nanosleep(&req, NULL);
}

static long long elapsed_ns(struct timespec start, struct timespec end){
    return (long long)(end.tv_sec - start.tv_sec) * NS_PER_SEC +
           (long long)(end.tv_nsec - start.tv_nsec);
}

static float clamp_float(float value, float min_value, float max_value){
    if(value < min_value){
        return min_value;
    }
    if(value > max_value){
        return max_value;
    }
    return value;
}

void voice_enhance(int16_t* samples){
    static float dc_prev_input[CHANNELS] = {0.0f};
    static float dc_prev_output[CHANNELS] = {0.0f};
    static float preemphasis_prev[CHANNELS] = {0.0f};
    static float noise_env[CHANNELS] = {0.0f};
    static float gate_gain_state[CHANNELS] = {1.0f};
    static float smooth_prev[CHANNELS] = {0.0f};

    const float sample_scale = 1.0f / 32768.0f;
    const float dc_alpha = 0.995f;
    const float preemphasis = 0.72f;
    const float preemphasis_mix = 0.18f;
    const float high_smooth_mix = 0.18f;
    const float gate_floor = 0.04f;
    const float gate_open = 0.045f;
    const float gate_close = 0.014f;
    const float compressor_threshold = 0.35f;
    const float compressor_ratio = 3.0f;
    const float makeup_gain = 1.18f;

    for(int i = 0; i < PB_SIZE; i++){
        int ch = i % CHANNELS;
        float x = (float)samples[i] * sample_scale;

        float dc_blocked = x - dc_prev_input[ch] + dc_alpha * dc_prev_output[ch];
        dc_prev_input[ch] = x;
        dc_prev_output[ch] = dc_blocked;

        float emphasized = dc_blocked - preemphasis * preemphasis_prev[ch];
        preemphasis_prev[ch] = dc_blocked;
        float y = (1.0f - preemphasis_mix) * dc_blocked + preemphasis_mix * emphasized;

        float abs_y = fabsf(y);
        float env_alpha = abs_y > noise_env[ch] ? 0.25f : 0.005f;
        noise_env[ch] += env_alpha * (abs_y - noise_env[ch]);

        float gate_gain;
        if(noise_env[ch] <= gate_close){
            gate_gain = gate_floor;
        }
        else if(noise_env[ch] >= gate_open){
            gate_gain = 1.0f;
        }
        else{
            float t = (noise_env[ch] - gate_close) / (gate_open - gate_close);
            gate_gain = gate_floor + t * (1.0f - gate_floor);
        }

        float gate_alpha = gate_gain > gate_gain_state[ch] ? 0.08f : 0.002f;
        gate_gain_state[ch] += gate_alpha * (gate_gain - gate_gain_state[ch]);
        y *= gate_gain_state[ch];

        float sign = y < 0.0f ? -1.0f : 1.0f;
        float magnitude = fabsf(y);
        if(magnitude > compressor_threshold){
            magnitude = compressor_threshold +
                        (magnitude - compressor_threshold) / compressor_ratio;
        }

        y = sign * magnitude * makeup_gain;
        y = tanhf(y * 1.05f);
        y = (1.0f - high_smooth_mix) * y + high_smooth_mix * smooth_prev[ch];
        smooth_prev[ch] = y;
        y = clamp_float(y, -0.98f, 0.98f);

        samples[i] = (int16_t)(y * 32767.0f);
    }
}

// --- Thread1: capture ---
void* capture_thread(void* arg){
    (void)arg;
    int16_t buffer[PB_SIZE];
    printf("[Thread] Capture thread started.\n");
    
    while(atomic_load_explicit(&running, memory_order_relaxed)){
        metrics_record_capture_cpu(sched_getcpu());

        int err = snd_pcm_readi(alsa_config.cap_handle, buffer, PERIOD_SIZE);
        if(err < 0){
            if(err == -EPIPE){
                metrics_record_capture_xrun();
            }
            alsa_hw_recover(alsa_config.cap_handle, err);
            continue;
        }
        
        // Drop the newest captured block if the process thread falls behind.
        if(ring_buffer_push(rb_cap_to_change, buffer, PB_SIZE)){
            metrics_record_capture_frames(PERIOD_SIZE);
        }
        else{
            metrics_record_capture_drop();
        }
    }

    return NULL;
}

// --- Thread2: process ---
void* process_thread(void* arg){
    (void)arg;
    int16_t buffer[PB_SIZE];
    printf("[Thread] Process thread started.\n");
    
    while(atomic_load_explicit(&running, memory_order_relaxed)){
        metrics_record_process_cpu(sched_getcpu());

        if(!ring_buffer_pop(rb_cap_to_change, buffer, PB_SIZE)){
            sleep_for_ns(PROCESS_IDLE_SLEEP_NS);
            continue;
        }
       
        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        voice_enhance(buffer);
        clock_gettime(CLOCK_MONOTONIC, &end);

        unsigned long long process_ns = (unsigned long long)elapsed_ns(start, end);
        metrics_record_process_block(PERIOD_SIZE, process_ns);
        
        // Drop this processed block if playback falls behind.
        if(!ring_buffer_push(rb_change_to_play, buffer, PB_SIZE)){
            metrics_record_process_drop();
        }
       
    }
    return NULL;
}

// --- Thread3: playback ---
void* playback_thread(void* arg){
    (void)arg;
    int16_t buffer[PB_SIZE];
    printf("[Thread] Playback thread started.\n");
    
    while(atomic_load_explicit(&running, memory_order_relaxed)){
        metrics_record_playback_cpu(sched_getcpu());

        if(!ring_buffer_pop(rb_change_to_play, buffer, PB_SIZE)){
            memset(buffer, 0, sizeof(buffer));
            metrics_record_playback_silence();
        }

        int err = snd_pcm_writei(alsa_config.play_handle, buffer, PERIOD_SIZE);
        if(err < 0){
            if(err == -EPIPE){
                metrics_record_playback_xrun();
            }
            alsa_hw_recover(alsa_config.play_handle, err);
        }
        else{
            metrics_record_playback_frames(PERIOD_SIZE);
        }
    }
    return NULL;
}

// stop running
void handle_process(int sig){
    (void)sig;
    printf("\n[System] Stop...!\n");
    atomic_store_explicit(&running, 0, memory_order_relaxed);
    metrics_stop();
}

int main(){
    pthread_t cap_tid, proc_tid, play_tid, metrics_tid;
    signal(SIGINT, handle_process);

    // Initial ALSA 
    if(alsa_hw_init(&alsa_config, ALSA_MODE_DUPLEX) < 0) return -1;

    // Initial Ring Buffer
    rb_cap_to_change = ring_buffer_init(PB_SIZE * RING_BUFFER_PERIODS);
    rb_change_to_play = ring_buffer_init(PB_SIZE * RING_BUFFER_PERIODS);
    if(!rb_cap_to_change || !rb_change_to_play){
        fprintf(stderr, "[System] Failed to initialize ring buffers.\n");
        ring_buffer_free(rb_cap_to_change);
        ring_buffer_free(rb_change_to_play);
        alsa_hw_close(&alsa_config);
        return -1;
    }

    metrics_set_ring_buffers(rb_cap_to_change, rb_change_to_play);

    // Create thread
    pthread_create(&cap_tid, NULL, capture_thread, NULL);
    pthread_create(&proc_tid, NULL, process_thread, NULL);
    pthread_create(&play_tid, NULL, playback_thread, NULL);
    pthread_create(&metrics_tid, NULL, metrics_thread, NULL);
    
    /*
    set_thread_affinity(cap_tid, 1);
    set_thread_affinity(play_tid, 2);
    set_thread_affinity(proc_tid, 3);
    */  
    
    printf("[System] Running. Press Ctrl+C to stop.\n");
    
    // Thread finish
    pthread_join(cap_tid, NULL);
    pthread_join(proc_tid, NULL);
    pthread_join(play_tid, NULL);
    pthread_join(metrics_tid, NULL);

    // Release
    ring_buffer_free(rb_cap_to_change);
    ring_buffer_free(rb_change_to_play);
    alsa_hw_close(&alsa_config);

    printf("[System] Shutdown.\n");
    return 0;
}
