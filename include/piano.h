#ifndef BASHBEATS_PIANO_H
#define BASHBEATS_PIANO_H

#include "data.h"

/* ── Piano keyboard overlay ──────────────────────────────────────────
 *
 * The keyboard shows 2 octaves starting from `base_octave`.
 * Key layout (QWERTY):
 *
 *   Lower octave white : a s d f g h j  -> C  D  E  F  G  A  B
 *   Lower octave black : 2 3   5 6 7    -> C# D#    F# G# A#
 *   Upper octave white : q w e r t y u  -> C  D  E  F  G  A  B
 *   Upper octave black : ! @ $ % ^      -> C# D#    F# G# A#
 *
 * Actual MIDI note = (base_octave + 1) * 12 + semitone_within_octave
 * So with base_octave=4: lower=C4..B4, upper=C5..B5
 *
 * Up/Down arrows shift base_octave within the track's allowed range.
 * ─────────────────────────────────────────────────────────────────── */

/* Draw the keyboard overlay.
 * base_octave : the lower of the 2 displayed octaves (e.g. 4 -> C4..B5).
 * note_min/max: the track's pianoroll range (for clamping display info). */
void piano_draw(int start_row, int start_col, int cur_track,
                int base_octave, int note_min, int note_max);

/* Map a raw keycode + base_octave to a MIDI note number.
 * Returns MIDI note on match, -1 if not a piano key. */
int piano_key_to_note(int ch, int base_octave);

/* Handle a keypress while piano overlay is active.
 * Returns 0 to stay, -1 to close. */
int piano_handle_key(int ch, Project *p, int cur_track, int base_octave);

#endif /* BASHBEATS_PIANO_H */
