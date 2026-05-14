#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alsa/asoundlib.h>
# include "wav_header.h"

int main(int argc, char *argv[]){
    int err;
    uint32_t rate = 16000;
    uint16_t channels = 2;
    uint16_t bits = 32;
    int seconds = 5;

    snd_pcm_t *capture;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S32_LE;

    const char *device = "hw:3,0";
    if(argc > 1) device = argv[1];

    printf(">>> WM8960 Activate: %s\n", device);
    if((err = snd_pcm_open(&capture, device, SND_PCM_STREAM_CAPTURE, 0)) < 0){
        fprintf(stderr, "Error: could not activate %s (%s)\n", device, snd_strerror(err));
        return 1;
    }
    
    // config device parameter
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(capture, hw_params);
    snd_pcm_hw_params_set_access(capture, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(capture, hw_params, format);
    snd_pcm_hw_params_set_rate_near(capture, hw_params, &rate, 0);
    snd_pcm_hw_params_set_channels(capture, hw_params, channels);
    
    // Setting WM8960
    if((err = snd_pcm_hw_params(capture, hw_params)) < 0){
        fprintf(stderr, "Error: could not apply setting (%s)\n", snd_strerror(err));
        return 1;
    }

    // Buffer
    snd_pcm_uframes_t buffer_frames = 128;
    uint32_t bytes_per_frames = channels * (bits/8);
    char *buffer = (char*)malloc(buffer_frames * bytes_per_frames);

    FILE *wav_file = fopen("record.wav", "wb");
    if(!wav_file){
        perror("Open file failed");
        return 1;
    }
    
    wav_header header;
    init_wav_header(&header, channels, rate, bits);
    fwrite(&header, sizeof(wav_header), 1, wav_file);
    
    uint32_t pcm_bytes = 0;
    int round = (seconds * rate) / buffer_frames;

    printf(">>> Start record (S32_LE, sample_rate: %uHz)...\n", rate);

    for(int i = 0; i < round; i++){
        snd_pcm_sframes_t frames_read = snd_pcm_readi(capture, buffer, buffer_frames);
        
        if(frames_read == -EPIPE){
            // overrun 
            snd_pcm_prepare(capture);
        }
        else if(frames_read < 0){
            fprintf(stderr, "r_error: %s\n", snd_strerror(frames_read));
            break;
        }
        else{
            size_t bytes_to_write = frames_read * bytes_per_frames;
            fwrite(buffer, 1, bytes_to_write, wav_file);
            pcm_bytes += bytes_to_write;
        }
    }
        
    // update wav header
    header.data_bytes = pcm_bytes;
    header.wav_size = 36 + pcm_bytes;
    fseek(wav_file, 0, SEEK_SET);
    fwrite(&header, sizeof(wav_header), 1, wav_file);

    printf(">>> Finish record, total %u bytes.\n", pcm_bytes);
    
    fclose(wav_file);
    free(buffer);
    snd_pcm_close(capture);
    return 0;
}
