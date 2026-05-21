#ifndef BASHBEATS_DATA_H
#define BASHBEATS_DATA_H

#include <stdint.h>
#include <pthread.h>

/* ── Directory constants ── */
#define SAVES_DIR       "saves"    /* relative to CWD: save files live here */
#define SAMPLES_DIR     "samples"  /* relative to CWD: instrument .wav files */

/* ── Piano/pianoroll layout ── */
#define MAX_TRACKS      16
#define MAX_EVENTS      4096       /* max note events per track */
#define TICKS_PER_QN    4          /* quarter note = 4 ticks */
#define PIANO_ROWS      24         /* 2 octaves shown in pianoroll */

/* ── MIDI note helpers ──
 * Each track has its own base_note (= "middle C" for that instrument).
 * The pianoroll shows [base_note - 12 .. base_note + 11] (24 semitones).
 * Use these macros with a Track pointer. */
#define TRACK_NOTE_MIN(t)  ((t)->base_note - 12)
#define TRACK_NOTE_MAX(t)  ((t)->base_note + 11)

/* ── Canonical MIDI note name (any note 0..127) ── */
const char *midi_note_name(int note);   /* e.g. 60 -> "C4", 61 -> "C#4" */
int         midi_note_octave(int note); /* e.g. 60 -> 4 */

/* ── Note event ── */
typedef struct {
    uint32_t start_tick;    /* absolute start tick */
    uint32_t duration_tick; /* duration in ticks   */
    uint8_t  note;          /* MIDI note number (0..127) */
    uint8_t  velocity;      /* velocity (1..127)   */
} NoteEvent;

/* ── Track ── */
typedef struct {
    char      name[32];
    char      instrument[128]; /* path to .wav relative to CWD, e.g. "samples/piano.wav" */
    int       base_note;       /* MIDI note treated as "middle C" for this track (default 60) */
    NoteEvent events[MAX_EVENTS];
    int       event_count;
    float     volume;          /* 0.0 ~ 1.0 */
    int       mute;            /* 0=play, 1=muted */
} Track;

/* ── Project ── */
typedef struct {
    char  title[64];
    int   bpm;              /* 20 ~ 300 */
    Track tracks[MAX_TRACKS];
    int   track_count;
} Project;

/* ── Shared mutex (defined in data.c) ── */
extern pthread_mutex_t g_project_mtx;

#endif /* BASHBEATS_DATA_H */
