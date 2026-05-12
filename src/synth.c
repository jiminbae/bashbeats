#include "synth.h"
#include "common.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double step_frames(int bpm) {
    bpm = BB_CLAMP(bpm, 40, 240);
    return (60.0 / (double)bpm) * BB_SAMPLE_RATE / 4.0;
}

void engine_init(Engine *e) {
    memset(e, 0, sizeof(*e));
    session_init(&e->session);
    pthread_mutex_init(&e->lock, NULL);
    e->current_step = 0;
    e->active_step = 0;
    e->selected_step = 0;
    e->selected_track = 0;
    e->playing = 1;
    e->frames_to_next_step = 1.0;
}

void engine_destroy(Engine *e) {
    for (int i = 0; i < BB_TRACKS; ++i) wav_free(&e->samples[i]);
    pthread_mutex_destroy(&e->lock);
}

static int16_t sample_at(const WavSample *s, double pos) {
    if (!s->pcm_mono || pos < 0 || pos >= (double)(s->frames - 1)) return 0;
    size_t i = (size_t)pos;
    double frac = pos - (double)i;
    double a = s->pcm_mono[i];
    double b = s->pcm_mono[i + 1];
    return (int16_t)(a + (b - a) * frac);
}

static void trigger_track(Engine *e, int track, float gain, float pitch) {
    if (track < 0 || track >= BB_TRACKS || !e->samples[track].pcm_mono) return;
    int slot = -1;
    for (int i = 0; i < BB_MAX_VOICES; ++i) {
        if (!e->voices[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    double resample = (double)e->samples[track].sample_rate / (double)BB_SAMPLE_RATE;
    e->voices[slot] = (Voice){ .active = 1, .track = track, .pos = 0.0, .inc = resample * pitch, .gain = gain };
}

void engine_render_block(Engine *e, int16_t *out, int frames) {
    memset(out, 0, sizeof(int16_t) * (size_t)frames);

    pthread_mutex_lock(&e->lock);
    for (int f = 0; f < frames; ++f) {
        if (e->playing) {
            e->frames_to_next_step -= 1.0;
            if (e->frames_to_next_step <= 0.0) {
                for (int t = 0; t < BB_TRACKS; ++t) {
                    if (e->session.pattern[t][e->current_step]) {
                        trigger_track(e, t, e->session.volume[t], e->session.pitch[t]);
                    }
                }
                e->active_step = e->current_step;
                e->current_step = (e->current_step + 1) % BB_STEPS;
                e->frames_to_next_step += step_frames(e->session.bpm);
            }
        }

        int mix = 0;
        for (int v = 0; v < BB_MAX_VOICES; ++v) {
            Voice *voice = &e->voices[v];
            if (!voice->active) continue;
            const WavSample *s = &e->samples[voice->track];
            if (voice->pos >= (double)(s->frames - 1)) { voice->active = 0; continue; }
            mix += (int)((float)sample_at(s, voice->pos) * voice->gain);
            voice->pos += voice->inc;
        }
        out[f] = bb_clip_i16(mix);
        e->rendered_frames++;
    }
    pthread_mutex_unlock(&e->lock);
}

void engine_toggle_step(Engine *e, int track, int step) {
    if (track < 0 || track >= BB_TRACKS || step < 0 || step >= BB_STEPS) return;
    pthread_mutex_lock(&e->lock);
    e->session.pattern[track][step] = !e->session.pattern[track][step];
    pthread_mutex_unlock(&e->lock);
}

void engine_adjust_bpm(Engine *e, int delta) {
    pthread_mutex_lock(&e->lock);
    e->session.bpm = BB_CLAMP(e->session.bpm + delta, 40, 240);
    pthread_mutex_unlock(&e->lock);
}

void engine_adjust_volume(Engine *e, int track, float delta) {
    if (track < 0 || track >= BB_TRACKS) return;
    pthread_mutex_lock(&e->lock);
    e->session.volume[track] = fmaxf(0.0f, fminf(1.5f, e->session.volume[track] + delta));
    pthread_mutex_unlock(&e->lock);
}

void engine_adjust_pitch(Engine *e, int track, float delta) {
    if (track < 0 || track >= BB_TRACKS) return;
    pthread_mutex_lock(&e->lock);
    e->session.pitch[track] = fmaxf(0.25f, fminf(4.0f, e->session.pitch[track] + delta));
    pthread_mutex_unlock(&e->lock);
}

void engine_snapshot(Engine *e, Session *out, int *step, int *sel_track, int *sel_step, int *playing) {
    pthread_mutex_lock(&e->lock);
    if (out) *out = e->session;
    if (step) *step = e->active_step;
    if (sel_track) *sel_track = e->selected_track;
    if (sel_step) *sel_step = e->selected_step;
    if (playing) *playing = e->playing;
    pthread_mutex_unlock(&e->lock);
}
