#define _POSIX_C_SOURCE 200809L
#include "piano.h"
#include "audio.h"
#include "input.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>

/* ── Key layout (semitone offsets within octave) ───────────────────
 *
 *  Lower octave (base_octave):
 *    White: a=C  s=D  d=E  f=F  g=G  h=A  j=B
 *    Black: 2=C# 3=D#     5=F# 6=G# 7=A#
 *
 *  Upper octave (base_octave + 1):
 *    White: q=C  w=D  e=E  r=F  t=G  y=A  u=B
 *    Black: !=C# @=D#     $=F# %=G# ^=A#
 *
 *  MIDI note = (base_octave + 1) * 12 + semitone
 *  e.g. base_octave=4: 'a' -> (4+1)*12 + 0 = 60 = C4
 * ─────────────────────────────────────────────────────────────────── */

/* semitone offsets: white[7], black[5] per octave */
static const int WHITE_ST[7] = { 0, 2, 4, 5, 7, 9, 11 }; /* C D E F G A B */
static const int BLACK_ST[5] = { 1, 3, 6, 8, 10 };        /* C# D# F# G# A# */

/* Key characters per octave row */
static const int WHITE_KEYS_LO[7] = { 'a','s','d','f','g','h','j' };
static const int WHITE_KEYS_HI[7] = { 'q','w','e','r','t','y','u' };
static const int BLACK_KEYS_LO[5] = { '2','3','5','6','7' };
static const int BLACK_KEYS_HI[5] = { '!','@','$','%','^' };

/* Key labels for display */
static const char *WHITE_NAMES[7] = { "C","D","E","F","G","A","B" };

/* has_black[w % 7]: 1 if white key w has a black key to its right */
static const int HAS_BLACK[7] = { 1,1,0,1,1,1,0 };

/* ── piano_key_to_note ── */
int piano_key_to_note(int ch, int base_octave)
{
    /* Lower octave white */
    for (int i = 0; i < 7; i++)
        if (ch == WHITE_KEYS_LO[i])
            return (base_octave + 1) * 12 + WHITE_ST[i];
    /* Lower octave black */
    for (int i = 0; i < 5; i++)
        if (ch == BLACK_KEYS_LO[i])
            return (base_octave + 1) * 12 + BLACK_ST[i];
    /* Upper octave white */
    for (int i = 0; i < 7; i++)
        if (ch == WHITE_KEYS_HI[i])
            return (base_octave + 2) * 12 + WHITE_ST[i];
    /* Upper octave black */
    for (int i = 0; i < 5; i++)
        if (ch == BLACK_KEYS_HI[i])
            return (base_octave + 2) * 12 + BLACK_ST[i];
    return -1;
}

/* ── piano_draw ──────────────────────────────────────────────────────
 *
 * Draws a 2-octave piano keyboard. The displayed octaves are
 * base_octave and base_octave+1.
 *
 * Layout (9 rows, 58 cols):
 *   r+0  top border + title
 *   r+1  black key labels (key binding)
 *   r+2  black key body
 *   r+3  black key bottom / white key top
 *   r+4  white key open body
 *   r+5  white key labels (binding + note name)
 *   r+6  white key lower body
 *   r+7  bottom border + octave info
 *
 * Columns: 14 white keys x 4 cols = 56 inner + 2 borders = 58 total
 * ─────────────────────────────────────────────────────────────────── */

#define WK    4   /* white key column width */
#define WN    14  /* total white keys (2 octaves) */
#define INNER (WN * WK)   /* 56 */
#define BORD  (INNER + 2) /* 58 */

void piano_draw(int start_row, int start_col, int cur_track,
                int base_octave, int note_min, int note_max)
{
    int r = start_row;
    int c = start_col;

    /* Precompute black key column positions */
    typedef struct { int col; int oct; int bi; } BKInfo;
    BKInfo bkeys[10]; int nbk = 0;
    {
        int bk_oct[2] = {0,0};
        for (int wi = 0; wi < WN; wi++) {
            int lw = wi % 7;
            if (HAS_BLACK[lw]) {
                int bleft = c + 1 + wi * WK + WK - 2;
                bkeys[nbk].col = bleft;
                bkeys[nbk].oct = wi / 7;
                bkeys[nbk].bi  = bk_oct[wi/7]++;
                nbk++;
            }
        }
    }

    /* Helper: MIDI note for a white key wi */
    #define WI_NOTE(wi) (((wi)/7 == 0) \
        ? (base_octave+1)*12 + WHITE_ST[(wi)%7] \
        : (base_octave+2)*12 + WHITE_ST[(wi)%7])
    #define BK_NOTE(bk) (((bk).oct == 0) \
        ? (base_octave+1)*12 + BLACK_ST[(bk).bi] \
        : (base_octave+2)*12 + BLACK_ST[(bk).bi])

    /* ── r+0  top border + title ── */
    mvaddch(r, c, ACS_ULCORNER); mvhline(r, c+1, ACS_HLINE, INNER);
    mvaddch(r, c+INNER+1, ACS_URCORNER);
    attron(A_BOLD);
    mvprintw(r, c+2, " PIANO  Oct%d/%d  Up/Dn:octave  (roll: %s-%s)  ^P/ESC:close ",
             base_octave, base_octave+1,
             midi_note_name(note_min), midi_note_name(note_max));
    attroff(A_BOLD);
    r++;

    /* ── r+1  black key labels ── */
    mvaddch(r, c, ACS_VLINE);
    for (int i = 1; i <= INNER; i++) mvaddch(r, c+i, ' ');
    for (int b = 0; b < nbk; b++) {
        int note = BK_NOTE(bkeys[b]);
        int in_range = (note >= note_min && note <= note_max);
        attron(A_REVERSE | (in_range ? A_BOLD : A_DIM));
        const char *lbl = (bkeys[b].oct == 0)
            ? (const char[]){(char)BLACK_KEYS_LO[bkeys[b].bi], 0}
            : (const char[]){(char)BLACK_KEYS_HI[bkeys[b].bi], 0};
        /* single-char key labels */
        char lo_lbl[2] = { (char)BLACK_KEYS_LO[bkeys[b].bi], 0 };
        char hi_lbl[2] = { (char)BLACK_KEYS_HI[bkeys[b].bi], 0 };
        mvprintw(r, bkeys[b].col,   "%s",
                 bkeys[b].oct == 0 ? lo_lbl : hi_lbl);
        mvaddch(r, bkeys[b].col+1, ' ');
        mvaddch(r, bkeys[b].col+2, ' ');
        attrset(A_NORMAL);
        (void)lbl;
    }
    mvaddch(r, c+INNER+1, ACS_VLINE);
    r++;

    /* ── r+2  black key body ── */
    mvaddch(r, c, ACS_VLINE);
    for (int i = 1; i <= INNER; i++) mvaddch(r, c+i, ' ');
    for (int b = 0; b < nbk; b++) {
        attron(A_REVERSE);
        mvaddch(r, bkeys[b].col,   ' ');
        mvaddch(r, bkeys[b].col+1, ' ');
        mvaddch(r, bkeys[b].col+2, ' ');
        attrset(A_NORMAL);
    }
    mvaddch(r, c+INNER+1, ACS_VLINE);
    r++;

    /* ── r+3  black key bottom / white key dividers ── */
    mvaddch(r, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi * WK;
        mvaddch(r, wc, ACS_VLINE);
        mvaddch(r, wc+1, ' '); mvaddch(r, wc+2, ' '); mvaddch(r, wc+3, ' ');
    }
    for (int b = 0; b < nbk; b++) {
        attron(A_REVERSE);
        mvaddch(r, bkeys[b].col,   ACS_HLINE);
        mvaddch(r, bkeys[b].col+1, ACS_HLINE);
        mvaddch(r, bkeys[b].col+2, ACS_HLINE);
        attrset(A_NORMAL);
    }
    mvaddch(r, c+INNER+1, ACS_VLINE);
    r++;

    /* ── r+4  white key open body ── */
    mvaddch(r, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi * WK;
        mvaddch(r, wc, ACS_VLINE);
        mvaddch(r, wc+1, ' '); mvaddch(r, wc+2, ' '); mvaddch(r, wc+3, ' ');
    }
    mvaddch(r, c+INNER+1, ACS_VLINE);
    r++;

    /* ── r+5  white key labels ── */
    mvaddch(r, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int lw  = wi % 7;
        int oct = wi / 7;
        int note = WI_NOTE(wi);
        int in_range = (note >= note_min && note <= note_max);
        int wc = c + 1 + wi * WK;
        mvaddch(r, wc, ACS_VLINE);
        /* key binding */
        attron(A_BOLD | (in_range ? 0 : A_DIM));
        char kb[2] = { (char)(oct == 0 ? WHITE_KEYS_LO[lw] : WHITE_KEYS_HI[lw]), 0 };
        mvprintw(r, wc+1, "%s", kb);
        attroff(A_BOLD | A_DIM);
        /* note name + octave */
        int display_oct = base_octave + oct;
        mvprintw(r, wc+2, "%s%d", WHITE_NAMES[lw], display_oct);
    }
    mvaddch(r, c+INNER+1, ACS_VLINE);
    r++;

    /* ── r+6  white key lower body ── */
    mvaddch(r, c, ACS_VLINE);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi * WK;
        mvaddch(r, wc, ACS_VLINE);
        mvaddch(r, wc+1, ' '); mvaddch(r, wc+2, ' '); mvaddch(r, wc+3, ' ');
    }
    mvaddch(r, c+INNER+1, ACS_VLINE);
    r++;

    /* ── r+7  bottom border ── */
    mvaddch(r, c, ACS_LLCORNER);
    for (int wi = 0; wi < WN; wi++) {
        int wc = c + 1 + wi * WK;
        mvaddch(r, wc, ACS_BTEE);
        mvhline(r, wc+1, ACS_HLINE, WK-1);
    }
    mvaddch(r, c+INNER+1, ACS_LRCORNER);

    #undef WI_NOTE
    #undef BK_NOTE
    (void)cur_track;
}

#undef WK
#undef WN
#undef INNER
#undef BORD

/* ── piano_handle_key ── */
int piano_handle_key(int ch, Project *p, int cur_track, int base_octave)
{
    if (ch == KEY_CTRL_P) return -1;
    int note = piano_key_to_note(ch, base_octave);
    if (note >= 0 && note <= 127) {
        float vol = (cur_track >= 0 && cur_track < p->track_count)
                    ? p->tracks[cur_track].volume : 1.0f;
        audio_note_on(cur_track, note, vol);
    }
    return 0;
}
