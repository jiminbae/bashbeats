#include "data.h"
#include <stdio.h>

/* ── Global mutex ── */
pthread_mutex_t g_project_mtx = PTHREAD_MUTEX_INITIALIZER;

/* ── Note name lookup ──
 * Returns a static string like "C4", "C#4", "D4" ... "B4".
 * Uses a rotating buffer so callers can use several results before overwrite. */
const char *midi_note_name(int note)
{
    static char bufs[4][8];
    static int  bi = 0;

    static const char *CHROMA[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    if (note < 0 || note > 127) return "??";
    int oct    = (note / 12) - 1;   /* MIDI: C4=60, oct4 */
    int chroma = note % 12;
    char *buf  = bufs[bi++ & 3];
    snprintf(buf, 8, "%s%d", CHROMA[chroma], oct);
    return buf;
}

int midi_note_octave(int note)
{
    return (note / 12) - 1;
}
