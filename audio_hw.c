#include "audio_hw.h"

static int hw_params(snd_pcm_t *handle){
    snd_pcm_hw_params_t *params;
    int err;
    uint32_t rate = SAMPLE_RATE;

    snd_pcm_hw_params_alloca(&params);

    // Setting parameters
    if((err = snd_pcm_hw_params_any(handle, params)) < 0){
        fprintf(stderr, "Could not initial hardware struct %s\n", snd_strerror(err));
        return -1;
    }

    if((err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0){
        fprintf(stderr, "Could not setting access mode %s\n", snd_strerror(err));
        return -1;
    }

    if((err = snd_pcm_hw_params_set_format(handle, params, FORMAT)) < 0){
        fprintf(stderr, "Could not setting format %s\n", snd_strerror(err));
        return -1;
    }

    if((err = snd_pcm_hw_params_set_channels(handle, params, CHANNELS)) < 0){
        fprintf(stderr, "Could not setting channels %s\n", snd_strerror(err));
        return -1;
    }

    unsigned int actual_rate;
    int dir;
    snd_pcm_uframes_t actual_period;
    
    if((err = snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0)) < 0){
        fprintf(stderr, "Could not setting sample rate %s\n", snd_strerror(err));
        return -1;
    }

    snd_pcm_uframes_t frames = PERIOD_SIZE;
    if((err = snd_pcm_hw_params_set_period_size_near(handle, params, &frames, 0)) < 0){
        fprintf(stderr, "Could not setting period size %s\n", snd_strerror(err));
        return -1;
    }
    
    /*
    snd_pcm_hw_params_get_rate(params, &actual_rate, &dir);
    snd_pcm_hw_params_get_period_size(params, &actual_period, &dir);
    const char *name;
    snd_pcm_stream_t stream = snd_pcm_stream(handle);
    name = (stream == SND_PCM_STREAM_CAPTURE) ? "CAPTURE" : "PLAYBACK";
    printf("[ALSA %s] Actual setting->rate : %u hz, period: %lu frames\n", name, actual_rate, (unsigned long)actual_period);
    */
    
    if((err = snd_pcm_hw_params(handle, params)) < 0){
        fprintf(stderr, "Could not set params into WM8960 %s\n", snd_strerror(err));
        return -1;
    }

    return 0;
}

int alsa_hw_init(ALSA_Config *config, ALSA_Mode mode){
	int err;
	
	config->cap_handle = NULL;
	config->play_handle = NULL;
	config->rate = SAMPLE_RATE;
	config->channels = CHANNELS;
	config->period = PERIOD_SIZE;
	
	if(mode == ALSA_MODE_CAP || mode == ALSA_MODE_DUPLEX){
		if((err = snd_pcm_open(&config->cap_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0)) < 0){
			fprintf(stderr, "Could not open capture device %s\n", snd_strerror(err));
            return -1;
		}

        if(hw_params(config->cap_handle) < 0) return -1;
        printf("[ALSA] capture device ready (%s)\n", PCM_DEVICE);
	}

	if(mode == ALSA_MODE_PLAY || mode == ALSA_MODE_DUPLEX){
		if((err = snd_pcm_open(&config->play_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0)) < 0){
			fprintf(stderr, "Could not open record device %s\n", snd_strerror(err));
            return -1;
		}

        if(hw_params(config->play_handle) < 0) return -1;
        printf("[ALSA] record device ready (%s)\n", PCM_DEVICE);
	}

    return 0;
}

void alsa_hw_close(ALSA_Config *config){
    if(config->cap_handle){
        snd_pcm_drain(config->cap_handle);
        snd_pcm_close(config->cap_handle);
        printf("[ALSA] capture device closed.\n");
    }

    if(config->play_handle){
        snd_pcm_drain(config->play_handle);
        snd_pcm_close(config->play_handle);
        printf("[ALSA] record device closed.\n");
    }
}

int alsa_hw_recover(snd_pcm_t *handle, int err){
    if(err == -EPIPE){
        fprintf(stderr, "[ALSA] buffer overflow, restart now...\n");
        int res = snd_pcm_prepare(handle);
        return res;
    }
    
    else if(err == -ESTRPIPE){
        fprintf(stderr, "[ALSA] system suspend, waiting restart...\n ");
        while((err = snd_pcm_resume(handle)) == -EAGAIN) sleep(1);
        if(err < 0) snd_pcm_prepare(handle);
        return 0;
    }
    return err;
}

