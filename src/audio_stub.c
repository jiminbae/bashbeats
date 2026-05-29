#include "audio.h"
#include "data.h"
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>

/* ── Audio Engine Stub ──────────────────────────────────────────────
 * Replaces the real audio engine during development / UI testing.
 *
 * WHAT THIS STUB DOES:
 *   - Logs every API call to /tmp/bashbeats.log.
 *   - Runs a background tick-timer thread so s_tick advances in real
 *     time at the correct BPM, making the UI playhead actually move.
 *
 * FOR THE NEXT CONTRIBUTOR — what real audio.c must implement:
 *   1. InstrumentSlot: load WAV per track (wav_load, mono conversion).
 *   2. Voice pool: pitch-shift via rate=pow(2,semitones/12), linear interp.
 *   3. render_loop thread: every RENDER_FRAMES (512) samples:
 *        a. advance s_tick_acc; fire NoteEvents at correct tick.
 *        b. mix all active voices → int16_t stereo buffer.
 *        c. stream_send(buf, RENDER_FRAMES).
 *   4. Replace audio_stub.c with audio.c in Makefile.
 *   See bashbeats_doc.pdf Part 2 for complete implementation spec.
 * ─────────────────────────────────────────────────────────────────── */

static volatile int      s_playing = 0;
static volatile int      s_paused  = 0;
static          int      s_bpm     = 120;
static volatile uint32_t s_tick    = 0;

static pthread_t    s_tick_tid;
static volatile int s_running = 0;

static void *tick_loop(void *arg)
{
    (void)arg;
    while (s_running) {
        if (s_playing && !s_paused) {
            double sec = 60.0 / (double)s_bpm / (double)TICKS_PER_QN;
            long ns = (long)(sec * 1e9);
            struct timespec ts = { ns / 1000000000L, ns % 1000000000L };
            nanosleep(&ts, NULL);
            if (s_playing && !s_paused) s_tick++;
        } else {
            struct timespec ts = { 0, 5000000L };
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

int audio_init(const char *sample_dir)
{
    fprintf(stderr, "[audio_stub] init(dir=%s)\n", sample_dir);
    s_running = 1;
    pthread_create(&s_tick_tid, NULL, tick_loop, NULL);
    return 0;
}

void audio_set_local_output(int enabled)
{
    fprintf(stderr, "[audio_stub] local_output=%s\n", enabled ? "on" : "off");
}

void audio_cleanup(void)
{
    s_running = 0;
    pthread_join(s_tick_tid, NULL);
    s_playing = s_paused = 0; s_tick = 0;
    fprintf(stderr, "[audio_stub] cleanup done\n");
}

void audio_load_instrument(int t, const char *p)
{ fprintf(stderr, "[audio_stub] load_instrument(track=%d, %s)\n", t, p?p:"null"); }

void audio_note_on(int t, int note, float vol)
{ fprintf(stderr, "[audio_stub] note_on(tr=%d n=%d[%s] vol=%.2f)\n", t, note, midi_note_name(note), vol); }

void audio_note_off(int t, int note)
{ fprintf(stderr, "[audio_stub] note_off(tr=%d n=%d)\n", t, note); }

void audio_play(Project *p)
{
    if (!p) return;
    s_bpm = p->bpm;
    s_playing = 1; s_paused = 0;
    /* s_tick intentionally NOT reset — starts from cursor */
    fprintf(stderr, "[audio_stub] play from tick=%u bpm=%d\n", s_tick, s_bpm);
}

void audio_pause(void)  { s_paused = 1;  fprintf(stderr, "[audio_stub] pause tick=%u\n", s_tick); }
void audio_resume(void) { s_paused = 0;  fprintf(stderr, "[audio_stub] resume tick=%u\n", s_tick); }
void audio_stop(void)   { s_playing = s_paused = 0; fprintf(stderr, "[audio_stub] stop\n"); }

void audio_set_bpm(int bpm)
{
    if (bpm < 20 || bpm > 300) return;
    fprintf(stderr, "[audio_stub] set_bpm %d->%d\n", s_bpm, bpm);
    s_bpm = bpm;
}

int audio_export_wav(Project *p, const char *path)
{
    fprintf(stderr, "[audio_stub] export_wav(project=%s, path=%s) not available in stub\n",
            p ? p->title : "null", path ? path : "null");
    return -1;
}

void audio_seek_tick(uint32_t tick) { s_tick = tick; }

int      audio_is_playing  (void) { return s_playing && !s_paused; }
int      audio_is_paused   (void) { return s_paused; }
uint32_t audio_current_tick(void) { return s_tick; }
int      audio_get_bpm     (void) { return s_bpm; }
