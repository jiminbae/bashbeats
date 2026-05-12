#ifndef BASHBEATS_WAV_H
#define BASHBEATS_WAV_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int sample_rate;
    int channels;
    int bits_per_sample;
    size_t frames;
    int16_t *pcm_mono;
} WavSample;

int wav_load_mono16(const char *path, WavSample *out);
void wav_free(WavSample *s);

#endif
