#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>

#include "audio_hw.h"
#include "metrics.h"

#define METRICS_LOG_PATH "metrics.log"

typedef struct{
    atomic_ullong capture_frames;
    atomic_ullong process_frames;
    atomic_ullong playback_frames;
    atomic_ullong capture_drop_blocks;
    atomic_ullong process_drop_blocks;
    atomic_ullong playback_silence_blocks;
    atomic_ullong capture_xruns;
    atomic_ullong playback_xruns;
    atomic_ullong process_blocks;
    atomic_ullong process_total_ns;
    atomic_ullong process_max_ns;
    atomic_int capture_cpu;
    atomic_int process_cpu;
    atomic_int playback_cpu;
} Metrics;

static Metrics metrics;
static RingBuffer* metrics_rb_input = NULL;
static RingBuffer* metrics_rb_output = NULL;
static atomic_int metrics_running = 1;

static void metrics_update_max_ns(unsigned long long value){
    unsigned long long current = atomic_load_explicit(&metrics.process_max_ns, memory_order_relaxed);

    while(value > current &&
          !atomic_compare_exchange_weak_explicit(&metrics.process_max_ns,
                                                 &current,
                                                 value,
                                                 memory_order_relaxed,
                                                 memory_order_relaxed)){
    }
}

void metrics_set_ring_buffers(RingBuffer* rb_input, RingBuffer* rb_output){
    metrics_rb_input = rb_input;
    metrics_rb_output = rb_output;
}

void metrics_stop(void){
    atomic_store_explicit(&metrics_running, 0, memory_order_relaxed);
}

void metrics_record_capture_cpu(int cpu){
    atomic_store_explicit(&metrics.capture_cpu, cpu, memory_order_relaxed);
}

void metrics_record_process_cpu(int cpu){
    atomic_store_explicit(&metrics.process_cpu, cpu, memory_order_relaxed);
}

void metrics_record_playback_cpu(int cpu){
    atomic_store_explicit(&metrics.playback_cpu, cpu, memory_order_relaxed);
}

void metrics_record_capture_frames(unsigned int frames){
    atomic_fetch_add_explicit(&metrics.capture_frames, frames, memory_order_relaxed);
}

void metrics_record_process_block(unsigned int frames, unsigned long long process_ns){
    atomic_fetch_add_explicit(&metrics.process_frames, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&metrics.process_blocks, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&metrics.process_total_ns, process_ns, memory_order_relaxed);
    metrics_update_max_ns(process_ns);
}

void metrics_record_playback_frames(unsigned int frames){
    atomic_fetch_add_explicit(&metrics.playback_frames, frames, memory_order_relaxed);
}

void metrics_record_capture_drop(void){
    atomic_fetch_add_explicit(&metrics.capture_drop_blocks, 1, memory_order_relaxed);
}

void metrics_record_process_drop(void){
    atomic_fetch_add_explicit(&metrics.process_drop_blocks, 1, memory_order_relaxed);
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

    unsigned long long last_capture_frames = 0;
    unsigned long long last_process_frames = 0;
    unsigned long long last_playback_frames = 0;
    unsigned long long last_capture_drop_blocks = 0;
    unsigned long long last_process_drop_blocks = 0;
    unsigned long long last_playback_silence_blocks = 0;
    unsigned long long last_capture_xruns = 0;
    unsigned long long last_playback_xruns = 0;
    unsigned long long last_process_blocks = 0;
    unsigned long long last_process_total_ns = 0;

    const double deadline_ms = ((double)PERIOD_SIZE * 1000.0) / (double)SAMPLE_RATE;

    FILE* log_file = fopen(METRICS_LOG_PATH, "w");
    if(!log_file){
        perror("[Metrics] Could not open metrics log");
        return NULL;
    }

    fprintf(log_file, "[Thread] Metrics thread started.\n");
    fflush(log_file);

    while(atomic_load_explicit(&metrics_running, memory_order_relaxed)){
        sleep(1);

        unsigned long long capture_frames = atomic_load_explicit(&metrics.capture_frames, memory_order_relaxed);
        unsigned long long process_frames = atomic_load_explicit(&metrics.process_frames, memory_order_relaxed);
        unsigned long long playback_frames = atomic_load_explicit(&metrics.playback_frames, memory_order_relaxed);
        unsigned long long capture_drop_blocks = atomic_load_explicit(&metrics.capture_drop_blocks, memory_order_relaxed);
        unsigned long long process_drop_blocks = atomic_load_explicit(&metrics.process_drop_blocks, memory_order_relaxed);
        unsigned long long playback_silence_blocks = atomic_load_explicit(&metrics.playback_silence_blocks, memory_order_relaxed);
        unsigned long long capture_xruns = atomic_load_explicit(&metrics.capture_xruns, memory_order_relaxed);
        unsigned long long playback_xruns = atomic_load_explicit(&metrics.playback_xruns, memory_order_relaxed);
        unsigned long long process_blocks = atomic_load_explicit(&metrics.process_blocks, memory_order_relaxed);
        unsigned long long process_total_ns = atomic_load_explicit(&metrics.process_total_ns, memory_order_relaxed);
        unsigned long long process_max_ns = atomic_exchange_explicit(&metrics.process_max_ns, 0, memory_order_relaxed);

        unsigned long long process_block_delta = process_blocks - last_process_blocks;
        unsigned long long process_total_ns_delta = process_total_ns - last_process_total_ns;
        double process_avg_ms = process_block_delta > 0
            ? ((double)process_total_ns_delta / (double)process_block_delta) / 1000000.0
            : 0.0;
        double process_max_ms = (double)process_max_ns / 1000000.0;

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

        fprintf(log_file, "[METRICS] cap=%llu/s proc=%llu/s play=%llu/s "
                "rb_in=%u%% rb_out=%u%% "
                "drop_in=%llu drop_out=%llu silence=%llu "
                "xrun_cap=%llu xrun_play=%llu "
                "proc_avg=%.3fms proc_max=%.3fms deadline=%.2fms "
                "cpu cap=%d proc=%d play=%d\n",
                capture_frames - last_capture_frames,
                process_frames - last_process_frames,
                playback_frames - last_playback_frames,
                rb_in_percent,
                rb_out_percent,
                capture_drop_blocks - last_capture_drop_blocks,
                process_drop_blocks - last_process_drop_blocks,
                playback_silence_blocks - last_playback_silence_blocks,
                capture_xruns - last_capture_xruns,
                playback_xruns - last_playback_xruns,
                process_avg_ms,
                process_max_ms,
                deadline_ms,
                atomic_load_explicit(&metrics.capture_cpu, memory_order_relaxed),
                atomic_load_explicit(&metrics.process_cpu, memory_order_relaxed),
                atomic_load_explicit(&metrics.playback_cpu, memory_order_relaxed));
        fflush(log_file);

        last_capture_frames = capture_frames;
        last_process_frames = process_frames;
        last_playback_frames = playback_frames;
        last_capture_drop_blocks = capture_drop_blocks;
        last_process_drop_blocks = process_drop_blocks;
        last_playback_silence_blocks = playback_silence_blocks;
        last_capture_xruns = capture_xruns;
        last_playback_xruns = playback_xruns;
        last_process_blocks = process_blocks;
        last_process_total_ns = process_total_ns;
    }

    fclose(log_file);
    return NULL;
}
