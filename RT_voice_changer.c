#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include "audio_hw.h"
#include "ring_buffer.h"

ALSA_Config alsa_config;
RingBuffer* rb_cap_to_change = NULL;  
RingBuffer* rb_change_to_play = NULL;
int running = 1;

#define PB_SIZE (PERIOD_SIZE * CHANNELS)

void set_thread_priority(pthread_t tid, int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    if (pthread_setschedparam(tid, SCHED_FIFO, &param) != 0) {
        perror("Failed to set thread priority (Try running with sudo)");
    }
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
    int16_t buffer[PB_SIZE];
    printf("[Thread] Capture thread started.\n");
    
    while(running){
        int err = snd_pcm_readi(alsa_config.cap_handle, buffer, PERIOD_SIZE);
        if(err < 0){
            alsa_hw_recover(alsa_config.cap_handle, err);
            continue;
        }
        
        // write
        ring_buffer_push(rb_cap_to_change, buffer, PB_SIZE);
    }

    return NULL;
}

// --- Thread2: process ---
void* process_thread(void* arg){
    int16_t buffer[PB_SIZE];
    printf("[Thread] Process thread started.\n");
    
    while(running){
        ring_buffer_pop(rb_cap_to_change, buffer, PB_SIZE);
        simple_lpf(buffer);
        ring_buffer_push(rb_change_to_play, buffer, PB_SIZE);
    }
    return NULL;
}

// --- Thread3: playback ---
void* playback_thread(void* arg){
    int16_t buffer[PB_SIZE];
    printf("[Thread] Playback thread started.\n");
    
    while(running){
        ring_buffer_pop(rb_change_to_play, buffer, PB_SIZE);
        int err = snd_pcm_writei(alsa_config.play_handle, buffer, PERIOD_SIZE);
        if(err < 0){
            alsa_hw_recover(alsa_config.play_handle, err);
        }
    }
    return NULL;
}

// stop running
void handle_process(int sig){
    printf("\n[System] Stop...!\n");
    running = 0;
}

int main(){
    pthread_t cap_tid, proc_tid, play_tid;
    signal(SIGINT, handle_process);

    // Initial ALSA 
    if(alsa_hw_init(&alsa_config, ALSA_MODE_DUPLEX) < 0) return -1;

    // Initial Ring Buffer
    rb_cap_to_change = ring_buffer_init(PB_SIZE * 20);
    rb_change_to_play = ring_buffer_init(PB_SIZE * 20);
    
    int16_t silence[PB_SIZE] = {0};
    for(int i = 0; i < 10; i++){
        ring_buffer_push(rb_change_to_play, silence, PB_SIZE);
    }
    
    // Create thread
    pthread_create(&cap_tid, NULL, capture_thread, NULL);
    pthread_create(&proc_tid, NULL, process_thread, NULL);
    pthread_create(&play_tid, NULL, playback_thread, NULL);
    
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


