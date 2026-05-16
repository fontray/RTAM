#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "ring_buffer.h"

RingBuffer* ring_buffer_init(int size){
    RingBuffer *rb = (RingBuffer*)malloc(sizeof(RingBuffer));
    rb->size = size;
    rb->buffer = (int16_t*)malloc(size * sizeof(int16_t));
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    
    pthread_mutex_init(&rb->lock, NULL);
    pthread_cond_init(&rb->not_fully, NULL);
    pthread_cond_init(&rb->not_empty, NULL);

    return rb;
}

void ring_buffer_push(RingBuffer* rb, int16_t* data, int frames){
    pthread_mutex_lock(&rb->lock);
    
    while((rb->size) - (rb->count) < frames){
        pthread_cond_wait(&rb->not_fully, &rb->lock);
    }
    
    int space_to_end = rb->size - rb->head;
    if (frames <= space_to_end) {
        memcpy(&rb->buffer[rb->head], data, frames * sizeof(int16_t));
    } else {
        memcpy(&rb->buffer[rb->head], data, space_to_end * sizeof(int16_t));
        memcpy(&rb->buffer[0], &data[space_to_end], (frames - space_to_end) * sizeof(int16_t));
    }
    
    rb->head = (rb->head + frames) % rb->size; 
    /*
    for(int i = 0; i < frames; i++){
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
    }
    */
    rb->count += frames;
    
    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->lock);
}

void ring_buffer_pop(RingBuffer* rb, int16_t* data, int frames){
    pthread_mutex_lock(&rb->lock);
    
    while(rb->count < frames){
        pthread_cond_wait(&rb->not_empty, &rb->lock);
    }
    
    int first_part = rb->size - rb->tail;
    
    if (frames <= first_part) {
        
        memcpy(data, &rb->buffer[rb->tail], frames * sizeof(int16_t));
    } else {
        memcpy(data, &rb->buffer[rb->tail], first_part * sizeof(int16_t));
        memcpy(&data[first_part], &rb->buffer[0], (frames - first_part) * sizeof(int16_t));
    }
    
    rb->tail = (rb->tail + frames) % rb->size;
    
    /*
    for(int i = 0; i < frames; i++){
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    */
    
    rb->count -= frames;
    
    pthread_cond_signal(&rb->not_fully);
    pthread_mutex_unlock(&rb->lock);
}

void ring_buffer_free(RingBuffer* rb){
    free(rb->buffer); 
    pthread_mutex_destroy(&rb->lock); 
    pthread_cond_destroy(&rb->not_fully); 
    pthread_cond_destroy(&rb->not_empty); 
    free(rb);
}










