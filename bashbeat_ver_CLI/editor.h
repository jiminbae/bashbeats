#ifndef BASHBEATS_EDITOR_H
#define BASHBEATS_EDITOR_H

#include "data.h"
#include "input.h"

/* ── Editor state ── */
typedef struct {
    EditorMode mode;        /* current mode */

    /* Track mode cursor */
    int track_cursor;       /* highlighted row in TRACK mode (0-based) */

    /* Pianoroll cursor */
    int cur_tick;           /* cursor column (tick, 0-based) */
    int cur_note;           /* cursor row (MIDI note 24~47) */
    int cur_track;          /* track being edited in EDIT mode */

    /* Viewport (pianoroll horizontal scroll) */
    int view_tick_start;    /* first visible tick */
    int view_tick_cols;     /* number of tick columns currently visible */

    /* Note input state (for , / . spanning) */
    int  span_active;       /* 1 if ',' was pressed and waiting for '.' */
    int  span_start_tick;   /* tick where ',' was pressed */
    int  span_note;         /* note fixed at the time ',' was pressed */

    /* File mode */
    char file_path[256];    /* current save/load path */
    char status_msg[128];   /* bottom status bar message */

    /* Stream info (display only) */
    int stream_clients;     /* cached client count for display */

    /* Track-mode playhead cursor (for seeking display) */
    uint32_t play_cursor;   /* tick position shown/seeked in TRACK mode */
} EditorState;

/* Global editor state (defined in editor.c) */
extern EditorState g_editor;

/* ── Lifecycle ── */
void editor_init (Project *p);  /* init ncurses + state */
void editor_run  (Project *p);  /* main event loop (blocks until quit) */
void editor_cleanup(void);      /* endwin + cleanup */

/* ── Per-mode draw functions ── */
void editor_draw_header  (const Project *p);
void editor_draw_track   (const Project *p);   /* TRACK mode overview */
void editor_draw_pianoroll(const Project *p);
void editor_draw_help    (void);
void editor_draw_play    (const Project *p);
void editor_draw_file    (const Project *p);
void editor_draw_status  (void);

/* ── Mode handlers (called from input_dispatch) ── */
void editor_set_mode(EditorMode m, Project *p);

/* ── Note operations ── */
void editor_place_note  (Project *p, uint32_t start, uint32_t dur, int note, uint8_t vel);
void editor_delete_note (Project *p, uint32_t tick, int note);
int  editor_note_at     (const Project *p, int track, uint32_t tick, int note);

#endif /* BASHBEATS_EDITOR_H */
