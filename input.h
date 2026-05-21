#ifndef BASHBEATS_INPUT_H
#define BASHBEATS_INPUT_H

/* ── Key input routing ──
 * Centralizes key constants and dispatches to the correct mode handler.
 * ncurses nodelay(TRUE) is set so getch() never blocks. */

/* Special key aliases (supplement ncurses KEY_* where needed) */
#define KEY_CTRL(x)  ((x) & 0x1f)   /* e.g. KEY_CTRL('e') == Ctrl+E */

#define KEY_CTRL_E   KEY_CTRL('e')   /* enter EDIT mode for selected track */
#define KEY_CTRL_P   KEY_CTRL('p')   /* piano overlay (in EDIT) / play-pause (in PLAY) */
#define KEY_CTRL_F   KEY_CTRL('f')   /* switch to FILE mode */
#define KEY_CTRL_S   KEY_CTRL('s')   /* save (in FILE mode) */
#define KEY_CTRL_T   KEY_CTRL('t')   /* switch to TRACK mode */
#define KEY_CTRL_Q   KEY_CTRL('q')   /* quit */

/* ── Editor modes ── */
typedef enum {
    MODE_TRACK = 0,  /* default: track list overview */
    MODE_EDIT,       /* pianoroll for selected track  */
    MODE_FILE,       /* file I/O                      */
} EditorMode;

/* ── input_dispatch ──
 * Called from the main loop with the character returned by getch().
 * Returns 0 to continue, -1 to quit. */
int input_dispatch(int ch);

#endif /* BASHBEATS_INPUT_H */
