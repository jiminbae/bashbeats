#ifndef BASHBEATS_SYNTH_H
#define BASHBEATS_SYNTH_H
#include "session.h"
#include "wav.h"
#include <pthread.h>
#include <stdint.h>

typedef struct {
    int active;
    int track;
    double pos;
    double inc;
    float gain;
} Voice;

typedef struct {
    Session session;
    WavSample samples[BB_TRACKS];
    Voice voices[BB_MAX_VOICES];
    pthread_mutex_t lock;
    int current_step;      /* next step to trigger */
    int active_step;       /* last step actually triggered by audio engine */
    int selected_step;
    int selected_track;
    int playing;
    uint64_t rendered_frames;
    double frames_to_next_step;
} Engine;

void engine_init(Engine *e);
void engine_destroy(Engine *e);
void engine_render_block(Engine *e, int16_t *out, int frames);
void engine_toggle_step(Engine *e, int track, int step);
void engine_adjust_bpm(Engine *e, int delta);
void engine_adjust_volume(Engine *e, int track, float delta);
void engine_adjust_pitch(Engine *e, int track, float delta);
void engine_snapshot(Engine *e, Session *out, int *active_step, int *sel_track, int *sel_step, int *playing);

#endif
