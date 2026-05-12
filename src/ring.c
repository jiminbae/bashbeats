#include "ring.h"
#include <string.h>

void ring_init(AudioRing *r) {
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->mutex, NULL);
    pthread_cond_init(&r->can_read, NULL);
    pthread_cond_init(&r->can_write, NULL);
}

void ring_close(AudioRing *r) {
    pthread_mutex_lock(&r->mutex);
    r->closed = 1;
    pthread_cond_broadcast(&r->can_read);
    pthread_cond_broadcast(&r->can_write);
    pthread_mutex_unlock(&r->mutex);
}

void ring_destroy(AudioRing *r) {
    pthread_mutex_destroy(&r->mutex);
    pthread_cond_destroy(&r->can_read);
    pthread_cond_destroy(&r->can_write);
}

int ring_push(AudioRing *r, const int16_t *block) {
    pthread_mutex_lock(&r->mutex);
    while (!r->closed && r->count == BB_RING_BLOCKS) pthread_cond_wait(&r->can_write, &r->mutex);
    if (r->closed) { pthread_mutex_unlock(&r->mutex); return 0; }
    memcpy(r->blocks[r->head], block, sizeof(r->blocks[r->head]));
    r->head = (r->head + 1) % BB_RING_BLOCKS;
    r->count++;
    pthread_cond_signal(&r->can_read);
    pthread_mutex_unlock(&r->mutex);
    return 1;
}

int ring_pop(AudioRing *r, int16_t *block) {
    pthread_mutex_lock(&r->mutex);
    while (!r->closed && r->count == 0) pthread_cond_wait(&r->can_read, &r->mutex);
    if (r->count == 0 && r->closed) { pthread_mutex_unlock(&r->mutex); return 0; }
    memcpy(block, r->blocks[r->tail], sizeof(r->blocks[r->tail]));
    r->tail = (r->tail + 1) % BB_RING_BLOCKS;
    r->count--;
    pthread_cond_signal(&r->can_write);
    pthread_mutex_unlock(&r->mutex);
    return 1;
}
