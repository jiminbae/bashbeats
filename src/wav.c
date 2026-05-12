#include "wav.h"
#include "common.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t size;
    char wave[4];
} RiffHeader;

typedef struct __attribute__((packed)) {
    char id[4];
    uint32_t size;
} ChunkHeader;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} FmtChunk;

static uint16_t le16(uint16_t x) { return x; }
static uint32_t le32(uint32_t x) { return x; }

int wav_load_mono16(const char *path, WavSample *out) {
    memset(out, 0, sizeof(*out));
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }

    RiffHeader rh;
    if (bb_read_all(fd, &rh, sizeof(rh)) != 1 || memcmp(rh.riff, "RIFF", 4) || memcmp(rh.wave, "WAVE", 4)) {
        fprintf(stderr, "invalid WAV: %s\n", path);
        close(fd);
        return -1;
    }

    FmtChunk fmt;
    int have_fmt = 0;
    off_t data_pos = 0;
    uint32_t data_size = 0;

    for (;;) {
        ChunkHeader ch;
        int r = bb_read_all(fd, &ch, sizeof(ch));
        if (r != 1) break;
        uint32_t sz = le32(ch.size);
        if (!memcmp(ch.id, "fmt ", 4)) {
            if (sz < sizeof(FmtChunk)) { close(fd); return -1; }
            if (bb_read_all(fd, &fmt, sizeof(fmt)) != 1) { close(fd); return -1; }
            if (sz > sizeof(fmt)) lseek(fd, (off_t)(sz - sizeof(fmt)), SEEK_CUR);
            have_fmt = 1;
        } else if (!memcmp(ch.id, "data", 4)) {
            data_pos = lseek(fd, 0, SEEK_CUR);
            data_size = sz;
            lseek(fd, (off_t)sz, SEEK_CUR);
        } else {
            lseek(fd, (off_t)sz, SEEK_CUR);
        }
        if (sz & 1) lseek(fd, 1, SEEK_CUR);
    }

    if (!have_fmt || data_pos <= 0 || le16(fmt.audio_format) != 1 || le16(fmt.bits_per_sample) != 16) {
        fprintf(stderr, "unsupported WAV, need PCM 16-bit: %s\n", path);
        close(fd);
        return -1;
    }

    int channels = le16(fmt.num_channels);
    int rate = (int)le32(fmt.sample_rate);
    size_t total_samples = data_size / sizeof(int16_t);
    size_t frames = total_samples / (size_t)channels;
    int16_t *raw = malloc(total_samples * sizeof(int16_t));
    int16_t *mono = calloc(frames, sizeof(int16_t));
    if (!raw || !mono) { free(raw); free(mono); close(fd); return -1; }

    lseek(fd, data_pos, SEEK_SET);
    if (bb_read_all(fd, raw, total_samples * sizeof(int16_t)) != 1) {
        free(raw); free(mono); close(fd); return -1;
    }

    for (size_t f = 0; f < frames; ++f) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) sum += raw[f * channels + c];
        mono[f] = (int16_t)(sum / channels);
    }

    free(raw);
    close(fd);
    out->sample_rate = rate;
    out->channels = 1;
    out->bits_per_sample = 16;
    out->frames = frames;
    out->pcm_mono = mono;
    return 0;
}

void wav_free(WavSample *s) {
    if (!s) return;
    free(s->pcm_mono);
    memset(s, 0, sizeof(*s));
}
