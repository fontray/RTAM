// define wav header
#ifndef WAV_HEADER_H
#define WAV_HEADER_H

#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct{
    // RIFF Header
    char riff_header[4]; // contains "RIFF"
    uint32_t wav_size;
    char wave_header[4]; //contains "WAVE"

    // Format Header
    char format[4];
    uint32_t fmt_chunk_size; // 16
    uint16_t audio_format; // 1 for PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate; // sample_rate * num_channels * Bytes Per Sample
    uint16_t sample_alignment; // num_channels * Bytes Per Sample
    uint16_t bit_depth; // Number of bits per sample

    // Data
    char data_header[4];
    uint32_t data_bytes; // Number of bytes in data. number of samples * num_channels * sample byte size

} wav_header;
#pragma pack(pop)

void init_wav_header(wav_header *header, uint16_t channels, uint32_t sample_rate, uint16_t bits_per_sample){
    memcpy(header->riff_header, "RIFF", 4);
    memcpy(header->wave_header, "WAVE", 4);
    memcpy(header->format, "fmt ", 4);
    header->fmt_chunk_size = 16;
    header->audio_format = 1;
    header->num_channels = channels;
    header->sample_rate = sample_rate;
    header->bit_depth = bits_per_sample;
    header->byte_rate = sample_rate * channels * (bits_per_sample/8);
    header->sample_alignment = channels * (bits_per_sample/8);
    memcpy(header->data_header, "data", 4);
    header->wav_size = 0;
    header->data_bytes = 0;
}

#endif

