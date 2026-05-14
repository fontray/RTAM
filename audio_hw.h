#ifndef AUDIO_HW_H
#define AUDIO_HW_H

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>

// Define hw_param
#define PCM_DEVICE "hw:3,0"
#define SAMPLE_RATE 44100
#define CHANNELS 2
#define FORMAT SND_PCM_FORMAT_S16_LE
#define PERIOD_SIZE (256 * 2)
#define BUFFER_SIZE (PERIOD_SIZE * 8)

typedef enum{
    ALSA_MODE_CAP = 0,
    ALSA_MODE_PLAY,
    ALSA_MODE_DUPLEX
} ALSA_Mode;

typedef struct{
    snd_pcm_t *cap_handle;
    snd_pcm_t *play_handle;
    uint32_t rate;
    uint16_t channels;
    snd_pcm_uframes_t period;
} ALSA_Config;

int alsa_hw_init(ALSA_Config *config, ALSA_Mode mode);

void alsa_hw_close(ALSA_Config *config);

int alsa_hw_recover(snd_pcm_t *handle, int err);

#endif

