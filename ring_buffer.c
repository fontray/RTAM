#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "ring_buffer.h"

static size_t ring_buffer_available(const RingBuffer* rb, size_t head, size_t tail){
    if(head >= tail){
        return head - tail;
    }
    return rb->size - tail + head;
}

static size_t ring_buffer_free_space(const RingBuffer* rb, size_t head, size_t tail){
    return rb->size - ring_buffer_available(rb, head, tail) - 1;
}

RingBuffer* ring_buffer_init(size_t size){
    if(size < 2){
        return NULL;
    }

    RingBuffer *rb = (RingBuffer*)malloc(sizeof(RingBuffer));
    if(!rb){
        return NULL;
    }

    rb->size = size;
    rb->buffer = (int16_t*)malloc(size * sizeof(int16_t));
    if(!rb->buffer){
        free(rb);
        return NULL;
    }

    atomic_init(&rb->head, 0);
    atomic_init(&rb->tail, 0);

    return rb;
}

int ring_buffer_push(RingBuffer* rb, const int16_t* data, size_t frames){
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if(frames == 0){
        return 1;
    }

    if(frames >= rb->size || ring_buffer_free_space(rb, head, tail) < frames){
        return 0;
    }

    size_t space_to_end = rb->size - head;
    if (frames <= space_to_end) {
        memcpy(&rb->buffer[head], data, frames * sizeof(int16_t));
    } else {
        memcpy(&rb->buffer[head], data, space_to_end * sizeof(int16_t));
        memcpy(&rb->buffer[0], &data[space_to_end], (frames - space_to_end) * sizeof(int16_t));
    }

    atomic_store_explicit(&rb->head, (head + frames) % rb->size, memory_order_release);
    return 1;
}

int ring_buffer_pop(RingBuffer* rb, int16_t* data, size_t frames){
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);

    if(frames == 0){
        return 1;
    }

    if(ring_buffer_available(rb, head, tail) < frames){
        return 0;
    }

    size_t first_part = rb->size - tail;

    if (frames <= first_part) {
        memcpy(data, &rb->buffer[tail], frames * sizeof(int16_t));
    } else {
        memcpy(data, &rb->buffer[tail], first_part * sizeof(int16_t));
        memcpy(&data[first_part], &rb->buffer[0], (frames - first_part) * sizeof(int16_t));
    }

    atomic_store_explicit(&rb->tail, (tail + frames) % rb->size, memory_order_release);
    return 1;
}

void ring_buffer_free(RingBuffer* rb){
    if(!rb){
        return;
    }

    free(rb->buffer); 
    free(rb);
}








