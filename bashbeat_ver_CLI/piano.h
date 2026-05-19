#ifndef BASHBEATS_PIANO_H
#define BASHBEATS_PIANO_H

#include "data.h"

/* ── Piano keyboard overlay (shown inside EDIT mode via Ctrl+P) ──
 *
 * Key → MIDI note mapping (qwerty layout, 2 octaves):
 *
 *   White keys: a s d f g h j  →  C1 D1 E1 F1 G1 A1 B1
 *               q w e r t y u  →  C2 D2 E2 F2 G2 A2 B2
 *
 *   Black keys: 2 3   5 6 7    →  C#1 D#1  F#1 G#1 A#1
 *               (top row)
 *
 * The overlay is rendered on a subwindow over the pianoroll area.
 * piano_handle_key() returns the MIDI note number (NOTE_MIN~NOTE_MAX)
 * if the key is a piano key, or -1 otherwise. */

/* Draw the piano keyboard overlay into a sub-region */
void piano_draw(int start_row, int start_col, int cur_track);

/* Map a raw keycode to a MIDI note number.
 * Returns note (24~47) on match, -1 if not a piano key. */
int piano_key_to_note(int ch);

/* Handle a keypress while piano overlay is active.
 * Calls audio_note_on / audio_note_off and adds note to pianoroll if recording.
 * Returns 0 to stay in piano mode, -1 if Ctrl+P pressed again (close overlay). */
int piano_handle_key(int ch, Project *p, int cur_track);

#endif /* BASHBEATS_PIANO_H */
