#ifndef BASHBEATS_SESSION_H
#define BASHBEATS_SESSION_H

#include "common.h"

typedef struct {
    int pattern[BB_TRACKS][BB_STEPS];
    float volume[BB_TRACKS];
    float pitch[BB_TRACKS];
    int bpm;
} Session;

void session_init(Session *s);
int session_save(const char *path, const Session *s);
int session_load(const char *path, Session *s);

#endif
