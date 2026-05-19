#include "audio.h"
#include <stdio.h>
#include <stdint.h>

/* ── Audio Engine Stub ──────────────────────────────────────────────
 * Every function logs its call to stderr so the editor can be developed
 * without a real audio engine.  Replace this file with audio.c later.
 * ─────────────────────────────────────────────────────────────────── */

static int      s_playing = 0;
static int      s_paused  = 0;
static int      s_bpm     = 120;
static uint32_t s_tick    = 0;

int audio_init(const char *sample_dir)
{
    fprintf(stderr, "[audio_stub] audio_init(sample_dir=\"%s\") -> 0\n", sample_dir);
    return 0;
}

void audio_cleanup(void)
{
    fprintf(stderr, "[audio_stub] audio_cleanup()\n");
    s_playing = s_paused = 0;
    s_tick = 0;
}

/* Called whenever a track's instrument path is set or changed. */
void audio_load_instrument(int track_idx, const char *wav_path)
{
    fprintf(stderr, "[audio_stub] audio_load_instrument(track=%d, path=\"%s\")\n",
            track_idx, wav_path ? wav_path : "(null)");
}

void audio_note_on(int track_idx, int note, float vol)
{
    fprintf(stderr, "[audio_stub] note_on(track=%d, note=%d[%s], vol=%.2f)\n",
            track_idx, note, midi_note_name(note), vol);
}

void audio_note_off(int track_idx, int note)
{
    fprintf(stderr, "[audio_stub] note_off(track=%d, note=%d[%s])\n",
            track_idx, note, midi_note_name(note));
}

void audio_play(Project *p)
{
    if (!p) return;
    fprintf(stderr, "[audio_stub] audio_play(\"%s\", bpm=%d, tracks=%d)\n",
            p->title, p->bpm, p->track_count);
    /* Notify engine of all current instruments */
    for (int i = 0; i < p->track_count; i++)
        audio_load_instrument(i, p->tracks[i].instrument);
    s_playing = 1; s_paused = 0; s_tick = 0;
}

void audio_pause(void)
{
    fprintf(stderr, "[audio_stub] audio_pause() [tick=%u]\n", s_tick);
    s_paused = 1;
}

void audio_resume(void)
{
    fprintf(stderr, "[audio_stub] audio_resume() [tick=%u]\n", s_tick);
    s_paused = 0;
}

void audio_stop(void)
{
    fprintf(stderr, "[audio_stub] audio_stop()\n");
    s_playing = s_paused = 0; s_tick = 0;
}

void audio_set_bpm(int bpm)
{
    if (bpm < 20 || bpm > 300) {
        fprintf(stderr, "[audio_stub] audio_set_bpm(%d) IGNORED (out of range)\n", bpm);
        return;
    }
    fprintf(stderr, "[audio_stub] audio_set_bpm(%d) [was %d]\n", bpm, s_bpm);
    s_bpm = bpm;
}

int      audio_is_playing  (void) { return s_playing && !s_paused; }
int      audio_is_paused   (void) { return s_paused; }
uint32_t audio_current_tick(void) { return s_tick; }
int      audio_get_bpm     (void) { return s_bpm; }

void audio_seek_tick(uint32_t tick)
{
    fprintf(stderr, "[audio_stub] audio_seek_tick(%u)\n", tick);
    s_tick = tick;
}
