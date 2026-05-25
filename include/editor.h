#ifndef BASHBEATS_EDITOR_H
#define BASHBEATS_EDITOR_H

#include "data.h"
#include "input.h"
#include <time.h>
#include <stdint.h>

/* ── Editor state ── */
typedef struct {
    EditorMode mode;        /* current mode */

    /* Track mode cursor */
    int track_cursor;       /* highlighted row in TRACK mode (0-based) */

    /* Pianoroll cursor */
    int cur_tick;           /* cursor column (tick, 0-based) */
    int cur_note;           /* cursor row (MIDI note) */
    int cur_track;          /* track being edited in EDIT mode */

    /* Viewport (pianoroll horizontal scroll) */
    int view_tick_start;    /* first visible tick */
    int view_tick_cols;     /* number of tick columns currently visible */

    /* Note input state (for , / . spanning) */
    int  span_active;       /* 1 if ',' was pressed and waiting for '.' */
    int  span_start_tick;
    int  span_note;

    /* File mode */
    char file_path[256];
    char status_msg[128];

    /* Stream info (display only) */
    int stream_clients;

    /* Track-mode playhead cursor */
    uint32_t play_cursor;

    /* Exit editor and return to intro screen */
    int exit_to_intro;
} EditorState;

/* Global editor state (defined in editor.c) */
extern EditorState g_editor;

/* ── Lifecycle ── */
void editor_init   (Project *p);
Project *editor_run(Project *p);
void editor_cleanup(void);

/* ── Per-mode draw functions ── */
void editor_draw_header   (const Project *p);
void editor_draw_track    (const Project *p);
void editor_draw_pianoroll(const Project *p);
void editor_draw_file     (const Project *p);
void editor_draw_help     (void);

/* ── Mode handlers ── */
void editor_set_mode(EditorMode m, Project *p);

/* ── Note operations ── */
void editor_place_note (Project *p, uint32_t start, uint32_t dur, int note, uint8_t vel);
void editor_delete_note(Project *p, uint32_t tick, int note);
int  editor_note_at    (const Project *p, int track, uint32_t tick, int note);

#endif /* BASHBEATS_EDITOR_H */
