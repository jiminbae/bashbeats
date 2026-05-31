#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "perform.h"
#include "audio.h"
#include "file_io.h"
#include "data.h"
#include "input.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

extern volatile sig_atomic_t g_sigint_received;

/* ── Keyboard layout ────────────────────────────────────────────────
 *
 * Standard piano-style QWERTY layout — black keys sit ABOVE white keys
 * on adjacent rows, just like a real piano.
 *
 * Lower octave (base_octave):
 *   Black:  s  d     g  h  j        (home row, gaps at E and B)
 *   White:  z  x  c  v  b  n  m     (bottom row)
 *           C  D  E  F  G  A  B
 *
 * Upper octave (base_octave + 1):
 *   Black:  2  3     5  6  7        (number row)
 *   White:  q  w  e  r  t  y  u     (top row)
 *           C  D  E  F  G  A  B
 *
 * This mirrors the physical arrangement of a piano:
 *   [2][3]   [5][6][7]   <- black keys (number row)
 *   [q][w][e][r][t][y][u]  <- white keys (QWERTY row)
 *   [s][d]   [g][h][j]   <- black keys (home row)
 *   [z][x][c][v][b][n][m]  <- white keys (bottom row)
 * ─────────────────────────────────────────────────────────────────── */

static const int WHITE_ST[7] = { 0, 2, 4, 5, 7, 9, 11 }; /* C D E F G A B */
static const int BLACK_ST[5] = { 1, 3, 6, 8, 10 };        /* C# D# F# G# A# */
static const int HAS_BLACK[7]= { 1, 1, 0, 1, 1, 1, 0 };

/* White keys: bottom row (lo oct), QWERTY row (hi oct) */
static const char WHITE_LO[7] = { 'z','x','c','v','b','n','m' };
static const char WHITE_HI[7] = { 'q','w','e','r','t','y','u' };

/* Black keys: home row (lo oct), number row (hi oct)
 * Positions match the gaps in HAS_BLACK: after C, D, F, G, A */
static const char BLACK_LO[5] = { 's','d','g','h','j' };
static const char BLACK_HI[5] = { '2','3','5','6','7' };

static const char *WHITE_NAMES[7] = { "C","D","E","F","G","A","B" };
/* BLACK_NAMES removed — not used in rendering */

/* Held-key visual state */
static int  s_held[128] = {0};

/* Current state */
static int  s_octave = 4;
static char s_instr[128]  = "(none)";
static char s_status[128] = "Ready.  Press a key to play.";

/* ── MIDI note helpers ── */
static int white_note(int wi, int base) { return (base+1+wi/7)*12 + WHITE_ST[wi%7]; }
static int black_note(int oi, int bi,  int base) { return (base+1+oi)*12 + BLACK_ST[bi]; }

/* ── key → MIDI note ── */
static int key_to_note(int ch, int base)
{
    for (int i=0;i<7;i++) if (ch==WHITE_LO[i]) return (base+1)*12+WHITE_ST[i];
    for (int i=0;i<5;i++) if (ch==BLACK_LO[i]) return (base+1)*12+BLACK_ST[i];
    for (int i=0;i<7;i++) if (ch==WHITE_HI[i]) return (base+2)*12+WHITE_ST[i];
    for (int i=0;i<5;i++) if (ch==BLACK_HI[i]) return (base+2)*12+BLACK_ST[i];
    return -1;
}

/* ── Draw ── */
static void perf_draw(void)
{
    erase();

    /* ── Header ── */
    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    mvprintw(0, 2, "BashBeats  PERFORMANCE MODE");
    mvprintw(0, 30, "| Instr: %-28.28s", s_instr);
    mvprintw(0, 62, "| Oct: %d/%d", s_octave, s_octave+1);
    mvprintw(0, 72, "| Up/Dn:octave  i:instr  ESC:back");
    attroff(COLOR_PAIR(8) | A_BOLD);
    mvhline(1, 0, ACS_HLINE, COLS);

    /* ── Keyboard graphic ───────────────────────────────────────────
     * White key width = WW=5 cols, 14 white keys, centred.
     * Rows (relative to KB_TOP=2):
     *   0   top border
     *   1-3 black key body  (label at row 1)
     *   4   black key bottom / white divider tops
     *   5-6 white key open body
     *   7   note name row
     *   8   key binding row (white)
     *   9   white key lower body
     *   10  bottom border
     *   11  blank
     *   12  key binding legend (black keys shown inline)
     */
    #define WW 5
    #define WN 14
    #define INNER (WN*WW)   /* 70 */

    int margin = (COLS - INNER) / 2;
    if (margin < 1) margin = 1;
    int KB_TOP = 2;

    /* Precompute black key columns */
    typedef struct { int col; int oi; int bi; } BK;
    BK bkeys[10]; int nbk = 0;
    { int bct[2]={0,0};
      for (int wi=0;wi<WN;wi++) {
        int lw=wi%7;
        if (HAS_BLACK[lw]) {
            bkeys[nbk].col = margin + wi*WW + WW - 2;
            bkeys[nbk].oi  = wi/7;
            bkeys[nbk].bi  = bct[wi/7]++;
            nbk++;
        }
      }
    }

    int r = KB_TOP;

    /* row 0: top border */
    mvaddch(r,margin-1,ACS_ULCORNER);
    mvhline(r,margin,ACS_HLINE,INNER);
    mvaddch(r,margin+INNER,ACS_URCORNER);
    r++;

    /* rows 1-3: black key body */
    for (int br=0; br<3; br++, r++) {
        mvaddch(r,margin-1,ACS_VLINE);
        for (int i=0;i<INNER;i++) mvaddch(r,margin+i,' ');
        mvaddch(r,margin+INNER,ACS_VLINE);
        for (int b=0;b<nbk;b++) {
            int note = black_note(bkeys[b].oi, bkeys[b].bi, s_octave);
            int held = (note>=0&&note<128) ? s_held[note] : 0;

            if (br==1) {
                /* Label row — draw all 3 chars at once in one attribute block */
                char lbl = bkeys[b].oi==0 ? BLACK_LO[bkeys[b].bi]
                                           : BLACK_HI[bkeys[b].bi];
                /* Background cells (sides) */
                if (held) attron(COLOR_PAIR(3)|A_BOLD);
                else      attron(A_REVERSE);
                mvaddch(r, bkeys[b].col+1, ' ');
                mvaddch(r, bkeys[b].col+2, ' ');
                attrset(A_NORMAL);
                /* Label char: black background (A_REVERSE) + yellow bold foreground */
                attron(COLOR_PAIR(3) | A_REVERSE | A_BOLD);
                mvaddch(r, bkeys[b].col, (chtype)lbl);
                attrset(A_NORMAL);
            } else {
                /* Non-label rows: solid black key body */
                if (held) attron(COLOR_PAIR(3)|A_BOLD);
                else      attron(A_REVERSE);
                mvaddch(r, bkeys[b].col,   ' ');
                mvaddch(r, bkeys[b].col+1, ' ');
                mvaddch(r, bkeys[b].col+2, ' ');
                attrset(A_NORMAL);
            }
        }
    }

    /* row 4: black key bottoms / white key dividers */
    mvaddch(r,margin-1,ACS_VLINE);
    for (int wi=0;wi<WN;wi++) {
        int wc=margin+wi*WW;
        mvaddch(r,wc,ACS_VLINE);
        for (int i=1;i<WW;i++) mvaddch(r,wc+i,' ');
    }
    mvaddch(r,margin+INNER,ACS_VLINE);
    for (int b=0;b<nbk;b++) { attron(A_REVERSE); mvhline(r,bkeys[b].col,ACS_HLINE,3); attrset(A_NORMAL); }
    r++;

    /* rows 5-6: white key open body (with held highlight) */
    for (int wr=0;wr<2;wr++,r++) {
        mvaddch(r,margin-1,ACS_VLINE);
        for (int wi=0;wi<WN;wi++) {
            int wc=margin+wi*WW;
            int note=white_note(wi,s_octave);
            int held=(note>=0&&note<128)?s_held[note]:0;
            mvaddch(r,wc,ACS_VLINE);
            if (held) { attron(COLOR_PAIR(3)|A_BOLD); for (int i=1;i<WW;i++) mvaddch(r,wc+i,ACS_BLOCK); attrset(A_NORMAL); }
            else      { for (int i=1;i<WW;i++) mvaddch(r,wc+i,' '); }
        }
        mvaddch(r,margin+INNER,ACS_VLINE);
    }

    /* row 7: note names */
    mvaddch(r,margin-1,ACS_VLINE);
    for (int wi=0;wi<WN;wi++) {
        int wc=margin+wi*WW, lw=wi%7, doct=s_octave+wi/7;
        int note=white_note(wi,s_octave);
        int held=(note>=0&&note<128)?s_held[note]:0;
        mvaddch(r,wc,ACS_VLINE);
        if (held) attron(COLOR_PAIR(3)|A_BOLD);
        mvprintw(r,wc+1,"%s%d",WHITE_NAMES[lw],doct);
        if (held) attrset(A_NORMAL);
        /* fill rest */
        for (int i=1+(int)strlen(WHITE_NAMES[lw])+1;i<WW;i++) mvaddch(r,wc+i,' ');
    }
    mvaddch(r,margin+INNER,ACS_VLINE);
    r++;

    /* row 8: white key bindings */
    mvaddch(r,margin-1,ACS_VLINE);
    for (int wi=0;wi<WN;wi++) {
        int wc=margin+wi*WW;
        char kb = wi/7==0 ? WHITE_LO[wi%7] : WHITE_HI[wi%7];
        mvaddch(r,wc,ACS_VLINE);
        attron(A_BOLD);
        mvprintw(r,wc+1,"[%c]",kb);
        attroff(A_BOLD);
        for (int i=4;i<WW;i++) mvaddch(r,wc+i,' ');
    }
    mvaddch(r,margin+INNER,ACS_VLINE);
    r++;

    /* row 9: white key lower body */
    mvaddch(r,margin-1,ACS_VLINE);
    for (int wi=0;wi<WN;wi++) {
        int wc=margin+wi*WW;
        int note=white_note(wi,s_octave);
        int held=(note>=0&&note<128)?s_held[note]:0;
        mvaddch(r,wc,ACS_VLINE);
        if (held) { attron(COLOR_PAIR(3)|A_BOLD); for (int i=1;i<WW;i++) mvaddch(r,wc+i,ACS_BLOCK); attrset(A_NORMAL); }
        else      { for (int i=1;i<WW;i++) mvaddch(r,wc+i,' '); }
    }
    mvaddch(r,margin+INNER,ACS_VLINE);
    r++;

    /* row 10: bottom border */
    mvaddch(r,margin-1,ACS_LLCORNER);
    for (int wi=0;wi<WN;wi++) { int wc=margin+wi*WW; mvaddch(r,wc,ACS_BTEE); mvhline(r,wc+1,ACS_HLINE,WW-1); }
    mvaddch(r,margin+INNER,ACS_LRCORNER);
    r++;

    #undef WW
    #undef WN
    #undef INNER

    mvhline(r,0,ACS_HLINE,COLS);
    r++;

    /* ── Key binding legend ── */
    /* Show black key layout inline with white keys */
    attron(COLOR_PAIR(1)|A_BOLD);
    mvprintw(r, margin, "Black keys (lo):"); attroff(COLOR_PAIR(1)|A_BOLD);
    attron(COLOR_PAIR(5));
    /* Map each black key to its position label */
    /* Layout: s=C#  d=D#  (gap E)  g=F#  h=G#  j=A# */
    mvprintw(r, margin+18, "[s]C#  [d]D#  --   [g]F#  [h]G#  [j]A#");
    attroff(COLOR_PAIR(5));
    r++;
    attron(COLOR_PAIR(1)|A_BOLD);
    mvprintw(r, margin, "Black keys (hi):"); attroff(COLOR_PAIR(1)|A_BOLD);
    attron(COLOR_PAIR(5));
    mvprintw(r, margin+18, "[2]C#  [3]D#  --   [5]F#  [6]G#  [7]A#");
    attroff(COLOR_PAIR(5));
    r++;

    /* ── Status ── */
    attron(COLOR_PAIR(3));
    mvprintw(r, 2, "%s", s_status);
    attroff(COLOR_PAIR(3));

    /* ── Help bar ── */
    attron(A_REVERSE);
    mvhline(LINES-1, 0, ' ', COLS);
    mvprintw(LINES-1, 1,
        " Up:oct+  Dn:oct-  i:instrument  ESC:back to menu"
        "  |  White: z-m (lo)  q-u (hi)   Black: s d g h j (lo)  2 3 5 6 7 (hi)");
    attroff(A_REVERSE);

    refresh();
}

/* ── Instrument picker ── */
static void perf_pick_instrument(void)
{
    char files[64][264];
    int  nf = file_list_instruments(files, 64);
    if (nf == 0) {
        snprintf(s_status, sizeof(s_status), "No .wav files in samples/");
        return;
    }

    nodelay(stdscr, FALSE);
    char query[64]={0}; int sel=0, done=0;

    while (!done) {
        int fidx[64]; int nflt=0;
        for (int i=0;i<nf;i++) {
            const char *sl=strrchr(files[i],'/'), *nm=sl?sl+1:files[i];
            int ok=!query[0];
            if (!ok) {
                int ql=(int)strlen(query), hl=(int)strlen(nm);
                for (int j=0; j<=hl-ql && !ok; j++) {
                    int m=1;
                    for (int k=0; k<ql && m; k++) {
                        char a=nm[j+k], b=query[k];
                        if (a>='A'&&a<='Z') a+=32;
                        if (b>='A'&&b<='Z') b+=32;
                        if (a!=b) m=0;
                    }
                    if (m) ok=1;
                }
            }
            if (ok) fidx[nflt++]=i;
        }
        if (sel>=nflt) sel=nflt>0?nflt-1:0;

        erase();
        attron(COLOR_PAIR(8)|A_BOLD); mvhline(0,0,' ',COLS);
        mvprintw(0,2,"SELECT INSTRUMENT -- Up/Dn:move  Enter:select  ESC:cancel  Type to filter");
        attroff(COLOR_PAIR(8)|A_BOLD);
        attron(COLOR_PAIR(3)); mvprintw(2,2,"Filter: [%-30s]",query); attroff(COLOR_PAIR(3));
        for (int i=0;i<nflt&&i<28;i++) {
            const char *sl=strrchr(files[fidx[i]],'/'),*nm=sl?sl+1:files[fidx[i]];
            if (i==sel) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(4+i,4,"> %-60s",nm); attrset(A_NORMAL); }
            else          { mvprintw(4+i,4,"  %-60s",nm); }
        }
        if (nflt==0) mvprintw(4,4,"  (no matches)");
        attron(A_REVERSE); mvhline(LINES-1,0,' ',COLS);
        mvprintw(LINES-1,1," Up/Down:navigate  Enter:select  ESC:cancel"); attroff(A_REVERSE);
        refresh();

        int ch=getch();
        if (ch==KEY_UP&&sel>0)      { sel--; continue; }
        if (ch==KEY_DOWN&&sel<nflt-1){ sel++; continue; }
        if (ch=='\n'||ch=='\r'||ch==KEY_ENTER) {
            if (nflt>0) {
                strncpy(s_instr,files[fidx[sel]],sizeof(s_instr)-1);
                s_instr[sizeof(s_instr)-1] = '\0';
                audio_load_instrument(0, s_instr);
                const char *shown = strrchr(s_instr, '/');
                shown = shown ? shown + 1 : s_instr;
                snprintf(s_status,sizeof(s_status),
                    "Instrument: %.80s", shown);
            }
            done=1;
        }
        if (ch==27) done=1;
        if (ch==KEY_BACKSPACE||ch==127) { int ql=(int)strlen(query); if(ql>0){query[ql-1]='\0';sel=0;} }
        else if (ch>=32&&ch<127&&(int)strlen(query)<63) { query[strlen(query)]=(char)ch; sel=0; }
    }
    nodelay(stdscr, TRUE);
}

/* ════════════════════════════════════════════════
 *  run_performance_mode
 * ════════════════════════════════════════════════ */
void run_performance_mode(void)
{
    nodelay(stdscr, TRUE);
    curs_set(0);
    memset(s_held, 0, sizeof(s_held));

    /* ── Select instrument on entry ── */
    perf_pick_instrument();
    /* If user cancelled (ESC), still enter perf mode with current instrument */

    {
        const char *shown = strrchr(s_instr, '/');
        shown = shown ? shown + 1 : s_instr;
        snprintf(s_status, sizeof(s_status),
                 "Instrument: %.64s  |  Up/Dn=octave  i=change instr  ESC=back", shown);
    }

    struct timespec last_seen[128];
    memset(last_seen, 0, sizeof(last_seen));

    while (1) {
        /* Check Ctrl+C */
        if (g_sigint_received) {
            g_sigint_received = 0;
            /* Simple y/n prompt */
            int pr = LINES/2 - 1, pc = COLS/2 - 18;
            attron(COLOR_PAIR(4)|A_BOLD);
            mvprintw(pr,   pc, "+------------------------------------+");
            mvprintw(pr+1, pc, "|  Quit BashBeats?  (y)es  (n)o      |");
            mvprintw(pr+2, pc, "+------------------------------------+");
            attroff(COLOR_PAIR(4)|A_BOLD);
            refresh();
            nodelay(stdscr, FALSE);
            int ch2 = getch();
            nodelay(stdscr, TRUE);
            if (ch2 == 'y' || ch2 == 'Y') { endwin(); exit(0); }
        }
        /* Release detection: 60ms silence → note off */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        for (int n=0;n<128;n++) {
            if (!s_held[n]) continue;
            double silence = (now.tv_sec -last_seen[n].tv_sec)
                           + (now.tv_nsec-last_seen[n].tv_nsec)*1e-9;
            if (silence > 0.06) {
                s_held[n] = 0;
                /* Note plays for its full bar duration — no early note_off */
            }
        }

        perf_draw();

        int ch = getch();
        if (ch == ERR) { struct timespec ts={0,8000000L}; nanosleep(&ts,NULL); continue; }

        /* ── ESC: back to intro (caller handles the loop) ── */
        if (ch == 27) break;

        /* ── Ctrl+C: quit entirely after confirmation ── */
        if (ch == KEY_CTRL_C) {
            g_sigint_received = 1;
            continue;
        }

        /* ── Octave ── */
        if (ch == KEY_UP)   { if (s_octave<7) s_octave++; snprintf(s_status,sizeof(s_status),"Octave: %d/%d",s_octave,s_octave+1); continue; }
        if (ch == KEY_DOWN) { if (s_octave>0) s_octave--; snprintf(s_status,sizeof(s_status),"Octave: %d/%d",s_octave,s_octave+1); continue; }

        /* ── Instrument ── */
        if (ch=='i'||ch=='I') { perf_pick_instrument(); continue; }

        /* ── Piano key ── */
        int note = key_to_note(ch, s_octave);
        if (note>=0&&note<128) {
            clock_gettime(CLOCK_MONOTONIC, &last_seen[note]);
            if (!s_held[note]) {
                s_held[note] = 1;
                static const char *CHROMA[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                snprintf(s_status,sizeof(s_status),
                    "Playing: %s%d (MIDI %d)",
                    CHROMA[note%12], note/12-1, note);
                audio_note_on_dur(0, note, 1.0f, TICKS_PER_QN * 4);
            } else {
                
                clock_gettime(CLOCK_MONOTONIC, &last_seen[note]);
            }
        }
    }

    /* Clear held state (notes play out their full duration naturally) */
    for (int n=0;n<128;n++) { s_held[n]=0; }
    memset(s_held, 0, sizeof(s_held));
    /* Returns to caller (main loop → show_intro again) */
}
