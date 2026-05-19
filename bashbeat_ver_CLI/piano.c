#include "piano.h"
#include "audio.h"
#include "input.h"
#include <ncurses.h>
#include <string.h>

/* ── Key -> MIDI note mapping ── */
typedef struct { int ch; int note; } KeyNote;

static const KeyNote KEY_MAP[] = {
    /* Octave 1 white: C1 D1 E1 F1 G1 A1 B1 */
    { 'a', 24 }, { 's', 26 }, { 'd', 28 }, { 'f', 29 },
    { 'g', 31 }, { 'h', 33 }, { 'j', 35 },
    /* Octave 1 black: C#1 D#1 F#1 G#1 A#1 */
    { '2', 25 }, { '3', 27 }, { '5', 30 }, { '6', 32 }, { '7', 34 },
    /* Octave 2 white: C2 D2 E2 F2 G2 A2 B2 */
    { 'q', 36 }, { 'w', 38 }, { 'e', 40 }, { 'r', 41 },
    { 't', 43 }, { 'y', 45 }, { 'u', 47 },
    /* Octave 2 black: C#2 D#2 F#2 G#2 A#2 */
    { '!', 37 }, { '@', 39 }, { '$', 42 }, { '%', 44 }, { '^', 46 },
    { -1, -1 }
};

int piano_key_to_note(int ch)
{
    for (int i = 0; KEY_MAP[i].ch != -1; i++) {
        if (KEY_MAP[i].ch == ch) return KEY_MAP[i].note;
    }
    return -1;
}

/* ── piano_draw ──────────────────────────────────────────────────────
 *
 * Draws a graphical 2-octave piano keyboard (C1-B2) as an overlay.
 *
 * Each white key is 4 columns wide.  14 white keys -> 56 cols inner.
 * Black keys are 3 cols wide and rendered with A_REVERSE over the
 * top 3 rows of the white-key area.
 *
 * Row layout (relative to start_row):
 *   r+0   top border
 *   r+1   title bar
 *   r+2   black key top row   (label: key binding)
 *   r+3   black key mid row   (body, reversed)
 *   r+4   black key bot row   (body, reversed; white key top)
 *   r+5   white key top open  (gap between black and white label)
 *   r+6   white key label row (key binding letter)
 *   r+7   white key bot row
 *   r+8   bottom border / note names
 *
 * Columns per white key : WK = 4
 * Black key width       : BK = 3  (centred on the gap between whites)
 * Inner width           : 14 * 4  = 56
 * Box width             : 56 + 2  = 58
 *
 * Black key positions (offset from left border, 0-indexed):
 *   Between white[i] and white[i+1]:  col = 1 + (i+1)*WK - 1  = i*WK + WK
 *   where col is the START of the 3-wide black key block.
 *
 * Black key presence per octave (7 whites, 0-based):
 *   After white 0 (C): yes  -> C#
 *   After white 1 (D): yes  -> D#
 *   After white 2 (E): NO
 *   After white 3 (F): yes  -> F#
 *   After white 4 (G): yes  -> G#
 *   After white 5 (A): yes  -> A#
 *   After white 6 (B): NO
 */

#define WK      4          /* white key column width */
#define WN      14         /* total white keys (2 octaves) */
#define INNER   (WN * WK)  /* 56 */
#define BOXW    (INNER + 2)/* 58 */
#define BOXH    9

/* has_black[w % 7]: 1 if white key w has a black key to its right */
static const int HAS_BLACK[7] = { 1, 1, 0, 1, 1, 1, 0 };

/* Black key keyboard bindings, oct0 then oct1 */
static const char *BK_BIND[2][5] = {
    { "2", "3", "5", "6", "7" },
    { "!", "@", "$", "%", "^" }
};

/* White key keyboard bindings */
static const char *WK_BIND[2][7] = {
    { "a", "s", "d", "f", "g", "h", "j" },
    { "q", "w", "e", "r", "t", "y", "u" }
};

/* White key note names (label row, one char each) */
static const char WK_NOTE[7] = { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };

void piano_draw(int start_row, int start_col, int cur_track)
{
    int r = start_row;
    int c = start_col;

    /* ── r+0  top border ─────────────────────────────────── */
    mvaddch(r, c, ACS_ULCORNER);
    mvhline(r, c+1, ACS_HLINE, INNER);
    mvaddch(r, c+INNER+1, ACS_URCORNER);

    /* ── r+1  title ──────────────────────────────────────── */
    mvaddch(r+1, c, ACS_VLINE);
    attron(A_BOLD);
    mvprintw(r+1, c+2, "PIANO KEYBOARD  (Ctrl+P: close)");
    attrset(A_NORMAL);
    /* fill to right border */
    for (int i = 33; i <= INNER; i++) mvaddch(r+1, c+i, ' ');
    mvaddch(r+1, c+INNER+1, ACS_VLINE);

    /* ── Precompute black key start columns ─────────────── */
    /*
     * For white key index wi (0..13):
     *   octave  = wi / 7
     *   local w = wi % 7
     *   if HAS_BLACK[local w]:
     *     black key left col = c + 1 + (wi+1)*WK - 2   (centred: WK=4, BK=3)
     *     i.e. right edge of white key minus 2
     *
     * We collect all (col, oct, bk_idx) triples.
     */
    typedef struct { int col; int oct; int bk; } BKInfo;
    BKInfo bkeys[10];
    int nbk = 0;
    {
        int bk_oct[2] = {0, 0};
        for (int wi = 0; wi < WN; wi++) {
            int oct = wi / 7;
            int lw  = wi % 7;
            if (HAS_BLACK[lw]) {
                /* centre 3-wide key on the gap: gap centre = c+1 + (wi+1)*WK - WK/2 */
                /* left edge of black key = gap_centre - 1 */
                int bleft = c + 1 + wi*WK + WK - 2;
                bkeys[nbk].col = bleft;
                bkeys[nbk].oct = oct;
                bkeys[nbk].bk  = bk_oct[oct]++;
                nbk++;
            }
        }
    }

    /* ── r+2  black key label row (binding) ─────────────── */
    mvaddch(r+2, c, ACS_VLINE);
    for (int i = 1; i <= INNER; i++) mvaddch(r+2, c+i, ' ');
    for (int b = 0; b < nbk; b++) {
        attron(A_REVERSE | A_BOLD);
        /* 3-wide black key: [binding_char][space][space] */
        mvprintw(r+2, bkeys[b].col,   "%s",
                 BK_BIND[bkeys[b].oct][bkeys[b].bk]);
        mvaddch(r+2, bkeys[b].col+1, ' ');
        mvaddch(r+2, bkeys[b].col+2, ' ');
        attrset(A_NORMAL);
    }
    mvaddch(r+2, c+INNER+1, ACS_VLINE);

    /* ── r+3  black key body ─────────────────────────────── */
    mvaddch(r+3, c, ACS_VLINE);
    for (int i = 1; i <= INNER; i++) mvaddch(r+3, c+i, ' ');
    for (int b = 0; b < nbk; b++) {
        attron(A_REVERSE);
        mvaddch(r+3, bkeys[b].col,   ' ');
        mvaddch(r+3, bkeys[b].col+1, ' ');
        mvaddch(r+3, bkeys[b].col+2, ' ');
        attrset(A_NORMAL);
    }
    mvaddch(r+3, c+INNER+1, ACS_VLINE);

    /* ── r+4  black key bottom / white key top open ─────── */
    mvaddch(r+4, c, ACS_VLINE);
    /* White key dividers */
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi*WK;
        mvaddch(r+4, wc, ACS_VLINE);
        mvaddch(r+4, wc+1, ' ');
        mvaddch(r+4, wc+2, ' ');
        mvaddch(r+4, wc+3, ' ');
    }
    /* Overlay black key bottoms */
    for (int b = 0; b < nbk; b++) {
        attron(A_REVERSE);
        mvaddch(r+4, bkeys[b].col,   ACS_HLINE);
        mvaddch(r+4, bkeys[b].col+1, ACS_HLINE);
        mvaddch(r+4, bkeys[b].col+2, ACS_HLINE);
        attrset(A_NORMAL);
    }
    mvaddch(r+4, c+INNER+1, ACS_VLINE);

    /* ── r+5  white key open body ───────────────────────── */
    mvaddch(r+5, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi*WK;
        mvaddch(r+5, wc,   ACS_VLINE);
        mvaddch(r+5, wc+1, ' ');
        mvaddch(r+5, wc+2, ' ');
        mvaddch(r+5, wc+3, ' ');
    }
    mvaddch(r+5, c+INNER+1, ACS_VLINE);

    /* ── r+6  white key labels (binding + note name) ────── */
    mvaddch(r+6, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int oct = wi / 7;
        int lw  = wi % 7;
        int wc  = c + 1 + wi*WK;
        mvaddch(r+6, wc, ACS_VLINE);
        /* key binding */
        attron(A_BOLD);
        mvprintw(r+6, wc+1, "%s", WK_BIND[oct][lw]);
        attrset(A_NORMAL);
        /* note name char */
        mvaddch(r+6, wc+2, WK_NOTE[lw]);
        mvaddch(r+6, wc+3, '0' + 1 + oct);  /* octave number: 1 or 2 */
    }
    mvaddch(r+6, c+INNER+1, ACS_VLINE);

    /* ── r+7  white key lower body ──────────────────────── */
    mvaddch(r+7, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi*WK;
        mvaddch(r+7, wc,   ACS_VLINE);
        mvaddch(r+7, wc+1, ' ');
        mvaddch(r+7, wc+2, ' ');
        mvaddch(r+7, wc+3, ' ');
    }
    mvaddch(r+7, c+INNER+1, ACS_VLINE);

    /* ── r+8  bottom border ─────────────────────────────── */
    mvaddch(r+8, c, ACS_LLCORNER);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi*WK;
        mvaddch(r+8, wc,   ACS_BTEE);
        mvhline(r+8, wc+1, ACS_HLINE, WK-1);
    }
    mvaddch(r+8, c+INNER+1, ACS_LRCORNER);

    (void)cur_track;
}

#undef WK
#undef WN
#undef INNER
#undef BOXW
#undef BOXH

/* ── piano_handle_key ── */
int piano_handle_key(int ch, Project *p, int cur_track)
{
    if (ch == KEY_CTRL_P) return -1;

    int note = piano_key_to_note(ch);
    if (note >= 0 && note <= 127) {
        float vol = (cur_track >= 0 && cur_track < p->track_count)
                    ? p->tracks[cur_track].volume : 1.0f;
        audio_note_on(cur_track, note, vol);
    }
    return 0;
}
