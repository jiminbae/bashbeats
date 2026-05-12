#ifndef BASHBEATS_COMMON_H
#define BASHBEATS_COMMON_H
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stddef.h>

#define BB_SAMPLE_RATE 44100
#define BB_CHANNELS 1
#define BB_DRUM_TRACKS 3
#define BB_PIANO_NOTES 7
#define BB_TRACKS 10
#define BB_STEPS 16
#define BB_BLOCK_FRAMES 256
#define BB_DEFAULT_PORT 7777
#define BB_RING_BLOCKS 96
#define BB_MAX_VOICES 48

#define BB_CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

int bb_set_nonblocking(int fd);
int bb_write_all(int fd, const void *buf, size_t len);
int bb_read_all(int fd, void *buf, size_t len);
int16_t bb_clip_i16(int value);
uint64_t bb_now_mono_ns(void);
void bb_sleep_until_ns(uint64_t deadline_ns);
void bb_sleep_ms(long ms);

#endif
