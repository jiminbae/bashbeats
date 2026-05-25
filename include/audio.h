#ifndef BASHBEATS_AUDIO_H
#define BASHBEATS_AUDIO_H

#include "data.h"
#include <stdint.h>

/* ── Init / Cleanup ── */
int  audio_init   (const char *sample_dir); /* 0=success, -1=fail */
void audio_cleanup(void);

/* ── Per-track instrument loading ──
 * Called whenever a track's instrument path changes.
 * The audio engine should load (or re-load) the .wav for that track slot.
 * Stub implementation just logs the call. */
void audio_load_instrument(int track_idx, const char *wav_path);

/* ── Realtime note control ── */
void audio_note_on (int track_idx, int note, float vol);
void audio_note_off(int track_idx, int note);

/* ── Project playback ── */
void audio_play   (Project *p);
void audio_pause  (void);
void audio_resume (void);
void audio_stop   (void);
void audio_set_bpm(int bpm);
int  audio_export_wav(Project *p, const char *path);

/* ── State query ── */
int      audio_is_playing  (void);
int      audio_is_paused   (void);
uint32_t audio_current_tick(void);
int      audio_get_bpm     (void);
void     audio_seek_tick   (uint32_t tick); /* seek playhead to tick */

#endif /* BASHBEATS_AUDIO_H */
