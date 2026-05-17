#ifndef METRICS_H
#define METRICS_H

#include "ring_buffer.h"

void metrics_set_ring_buffers(RingBuffer* rb_input, RingBuffer* rb_output);
void metrics_stop(void);

void metrics_record_capture_drop(void);
void metrics_record_playback_silence(void);
void metrics_record_capture_xrun(void);
void metrics_record_playback_xrun(void);

void* metrics_thread(void* arg);

#endif
