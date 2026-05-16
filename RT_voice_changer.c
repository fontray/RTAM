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
#include "ring_buffer.h"

ALSA_Config alsa_config;
RingBuffer* rb_cap_to_change = NULL;  
RingBuffer* rb_change_to_play = NULL;
static atomic_int running = 1;

#define PB_SIZE (PERIOD_SIZE * CHANNELS)
#define RING_BUFFER_PERIODS 4
#define PROCESS_IDLE_SLEEP_NS 50000L

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

void simple_lpf(int16_t* samples){
    static float last_sample = 0.0f;
    float alpha = 0.4f;

    for(int i = 0; i < PB_SIZE; i++){
        float current = alpha * samples[i] + (1.0f - alpha) * last_sample;
        samples[i] = (int16_t)current;
        last_sample = current;
    }
}

// --- Thread1: capture ---
void* capture_thread(void* arg){
    (void)arg;
    int16_t buffer[PB_SIZE];
    printf("[Thread] Capture thread started.\n");
    
    while(atomic_load_explicit(&running, memory_order_relaxed)){
        int err = snd_pcm_readi(alsa_config.cap_handle, buffer, PERIOD_SIZE);
        if(err < 0){
            alsa_hw_recover(alsa_config.cap_handle, err);
            continue;
        }
        
        // Drop the newest captured block if the process thread falls behind.
        ring_buffer_push(rb_cap_to_change, buffer, PB_SIZE);
    }

    return NULL;
}

// --- Thread2: process ---
void* process_thread(void* arg){
    (void)arg;
    int16_t buffer[PB_SIZE];
    printf("[Thread] Process thread started.\n");
    
    while(atomic_load_explicit(&running, memory_order_relaxed)){
        if(!ring_buffer_pop(rb_cap_to_change, buffer, PB_SIZE)){
            sleep_for_ns(PROCESS_IDLE_SLEEP_NS);
            continue;
        }
       
        simple_lpf(buffer);
        
        // Drop this processed block if playback falls behind.
        ring_buffer_push(rb_change_to_play, buffer, PB_SIZE);
       
    }
    return NULL;
}

// --- Thread3: playback ---
void* playback_thread(void* arg){
    (void)arg;
    int16_t buffer[PB_SIZE];
    printf("[Thread] Playback thread started.\n");
    
    while(atomic_load_explicit(&running, memory_order_relaxed)){
        if(!ring_buffer_pop(rb_change_to_play, buffer, PB_SIZE)){
            memset(buffer, 0, sizeof(buffer));
        }

        int err = snd_pcm_writei(alsa_config.play_handle, buffer, PERIOD_SIZE);
        if(err < 0){
            alsa_hw_recover(alsa_config.play_handle, err);
        }
    }
    return NULL;
}

// stop running
void handle_process(int sig){
    (void)sig;
    printf("\n[System] Stop...!\n");
    atomic_store_explicit(&running, 0, memory_order_relaxed);
}

int main(){
    pthread_t cap_tid, proc_tid, play_tid;
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
    // Create thread
    pthread_create(&cap_tid, NULL, capture_thread, NULL);
    pthread_create(&proc_tid, NULL, process_thread, NULL);
    pthread_create(&play_tid, NULL, playback_thread, NULL);
    
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

    // Release
    ring_buffer_free(rb_cap_to_change);
    ring_buffer_free(rb_change_to_play);
    alsa_hw_close(&alsa_config);

    printf("[System] Shutdown.\n");
    return 0;
}
