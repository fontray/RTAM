#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "audio_hw.h"
#include "metrics.h"

#define METRICS_LOG_PATH "metrics.log"

typedef struct{
    atomic_ullong capture_drop_blocks;
    atomic_ullong playback_silence_blocks;
    atomic_ullong capture_xruns;
    atomic_ullong playback_xruns;
} Metrics;

static Metrics metrics;
static RingBuffer* metrics_rb_input = NULL;
static RingBuffer* metrics_rb_output = NULL;
static atomic_int metrics_running = 1;

void metrics_set_ring_buffers(RingBuffer* rb_input, RingBuffer* rb_output){
    metrics_rb_input = rb_input;
    metrics_rb_output = rb_output;
}

void metrics_stop(void){
    atomic_store_explicit(&metrics_running, 0, memory_order_relaxed);
}

void metrics_record_capture_drop(void){
    atomic_fetch_add_explicit(&metrics.capture_drop_blocks, 1, memory_order_relaxed);
}

void metrics_record_playback_silence(void){
    atomic_fetch_add_explicit(&metrics.playback_silence_blocks, 1, memory_order_relaxed);
}

void metrics_record_capture_xrun(void){
    atomic_fetch_add_explicit(&metrics.capture_xruns, 1, memory_order_relaxed);
}

void metrics_record_playback_xrun(void){
    atomic_fetch_add_explicit(&metrics.playback_xruns, 1, memory_order_relaxed);
}

void* metrics_thread(void* arg){
    (void)arg;

    unsigned long long last_capture_drop_blocks = 0;
    unsigned long long last_playback_silence_blocks = 0;
    unsigned long long last_capture_xruns = 0;
    unsigned long long last_playback_xruns = 0;

    FILE* log_file = fopen(METRICS_LOG_PATH, "w");
    if(!log_file){
        perror("[Metrics] Could not open metrics log");
        return NULL;
    }

    fprintf(log_file, "[Thread] Metrics thread started.\n");
    fflush(log_file);

    while(atomic_load_explicit(&metrics_running, memory_order_relaxed)){
        sleep(1);

        unsigned long long capture_drop_blocks = atomic_load_explicit(&metrics.capture_drop_blocks, memory_order_relaxed);
        unsigned long long playback_silence_blocks = atomic_load_explicit(&metrics.playback_silence_blocks, memory_order_relaxed);
        unsigned long long capture_xruns = atomic_load_explicit(&metrics.capture_xruns, memory_order_relaxed);
        unsigned long long playback_xruns = atomic_load_explicit(&metrics.playback_xruns, memory_order_relaxed);

        size_t rb_in_available = metrics_rb_input ? ring_buffer_read_available(metrics_rb_input) : 0;
        size_t rb_out_available = metrics_rb_output ? ring_buffer_read_available(metrics_rb_output) : 0;
        size_t rb_in_capacity = metrics_rb_input ? ring_buffer_capacity(metrics_rb_input) : 0;
        size_t rb_out_capacity = metrics_rb_output ? ring_buffer_capacity(metrics_rb_output) : 0;
        unsigned int rb_in_percent = rb_in_capacity > 0
            ? (unsigned int)((rb_in_available * 100U) / rb_in_capacity)
            : 0;
        unsigned int rb_out_percent = rb_out_capacity > 0
            ? (unsigned int)((rb_out_available * 100U) / rb_out_capacity)
            : 0;

        fprintf(log_file, "[METRICS] "
                "rb_in=%u%% rb_out=%u%% "
                "drop_in=%llu silence=%llu "
                "xrun_cap=%llu xrun_play=%llu\n",
                rb_in_percent,
                rb_out_percent,
                capture_drop_blocks - last_capture_drop_blocks,
                playback_silence_blocks - last_playback_silence_blocks,
                capture_xruns - last_capture_xruns,
                playback_xruns - last_playback_xruns); 
        fflush(log_file);

        last_capture_drop_blocks = capture_drop_blocks;
        last_playback_silence_blocks = playback_silence_blocks;
        last_capture_xruns = capture_xruns;
        last_playback_xruns = playback_xruns;
    }

    fclose(log_file);
    return NULL;
}
