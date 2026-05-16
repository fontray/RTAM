#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>



typedef struct{
    int16_t* buffer;
    size_t size;

    atomic_size_t head;
    char head_padding[64];

    atomic_size_t tail;
    char tail_padding[64];
} RingBuffer;

RingBuffer* ring_buffer_init(size_t size);


void ring_buffer_free(RingBuffer* rb);


int ring_buffer_push(RingBuffer* rb, const int16_t* data, size_t frames);
int ring_buffer_pop(RingBuffer* rb, int16_t* data, size_t frames);



#endif
