#ifndef BASHBEATS_RING_H
#define BASHBEATS_RING_H
#include "common.h"
#include <pthread.h>
#include <stdint.h>

typedef struct {
    int16_t blocks[BB_RING_BLOCKS][BB_BLOCK_FRAMES];
    int head, tail, count;
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t can_read;
    pthread_cond_t can_write;
} AudioRing;

void ring_init(AudioRing *r);
void ring_close(AudioRing *r);
void ring_destroy(AudioRing *r);
int ring_push(AudioRing *r, const int16_t *block);
int ring_pop(AudioRing *r, int16_t *block);

#endif
