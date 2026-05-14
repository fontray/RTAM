#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

typedef struct{
    int16_t* buffer;
    int size;
    int head;
    int tail;
    int count;
    
    pthread_mutex_t lock;
    pthread_cond_t not_fully;
    pthread_cond_t not_empty;
} RingBuffer;

RingBuffer* ring_buffer_init(int size);
void ring_buffer_free(RingBuffer* rb);

void ring_buffer_push(RingBuffer* rb, int16_t* data, int frames);
void ring_buffer_pop(RingBuffer* rb, int16_t* data, int frames);


#endif
