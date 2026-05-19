#include "editor.h"
#include "piano.h"
#include "audio.h"
#include "file_io.h"
#include "stream.h"
#include "input.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* ── Layout constants ── */
#define HEADER_ROWS     3   /* top header bar height */
#define HELP_ROWS       3   /* bottom help bar height */
#define STATUS_ROW      1   /* status message row above help */
#define TRACK_COLS      10  /* left track name sidebar width */
#define NOTE_LABEL_COLS 5   /* note label column width (e.g. "C#2 ") */
#define TICK_COL_WIDTH  3   /* pixels per tick column */

/* ── Global editor state ── */
EditorState g_editor;

/* ── Forward declarations ── */
static void draw_all(const Project *p);
static void handle_track_key(int ch, Project *p);
static int  pick_instrument(char out[128]);
static void handle_edit_key(int ch, Project *p);
static void handle_play_key(int ch, Project *p);
static void handle_file_key(int ch, Project **pp);

/* ═══════════════════════════════════════════════
 *  INIT / CLEANUP
 * ═══════════════════════════════════════════════ */

void editor_init(Project *p)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);   /* non-blocking getch() — required for piano */
    curs_set(1);

    if (has_colors()) {
        start_color();
        use_default_colors();
        /* Color pairs */
        init_pair(1,  COLOR_CYAN,    -1);  /* header / mode label */
        init_pair(2,  COLOR_GREEN,   -1);  /* note block */
        init_pair(3,  COLOR_YELLOW,  -1);  /* cursor / selection */
        init_pair(4,  COLOR_RED,     -1);  /* muted / error */
        init_pair(5,  COLOR_MAGENTA, -1);  /* piano overlay */
        init_pair(6,  COLOR_WHITE,   -1);  /* normal text */
        init_pair(7,  COLOR_BLUE,    -1);  /* track sidebar */
        init_pair(8,  COLOR_BLACK,   COLOR_CYAN);    /* header bg */
        init_pair(9,  COLOR_BLACK,   COLOR_GREEN);   /* active note cursor */
        init_pair(10, COLOR_BLACK,   COLOR_YELLOW);  /* span preview */
    }

    /* Redirect stderr to log file so debug prints don't corrupt ncurses */
    freopen("/tmp/bashbeats.log", "a", stderr);

    memset(&g_editor, 0, sizeof(g_editor));
    g_editor.mode            = MODE_TRACK;
    g_editor.track_cursor    = 0;
    g_editor.cur_tick        = 0;
    g_editor.cur_note        = p->tracks[0].base_note;   /* start on base note */
    g_editor.cur_track       = 0;
    g_editor.view_tick_start = 0;
    g_editor.span_active     = 0;

    /* Default save path: saves/<title>.bbeat */
    file_default_path(p->title, g_editor.file_path, sizeof(g_editor.file_path));
    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "BashBeats ready. Up/Down=select track, Ctrl+E=edit, Ctrl+Q=quit.");

    (void)p;
}

void editor_cleanup(void)
{
    endwin();
}

/* ═══════════════════════════════════════════════
 *  DRAW HELPERS
 * ═══════════════════════════════════════════════ */

/* Returns number of tick columns that fit in the pianoroll area */
static int visible_tick_cols(void)
{
    int max_cols = COLS - NOTE_LABEL_COLS - TRACK_COLS;
    return max_cols / TICK_COL_WIDTH;
}

/* ── Header bar ── */
void editor_draw_header(const Project *p)
{
    attron(COLOR_PAIR(8) | A_BOLD);
    for (int c = 0; c < COLS; c++) mvaddch(0, c, ' ');

    const char *mode_str[] = { "TRACK", "EDIT", "PLAY", "FILE", "PIANO" };
    const char *mode_label = mode_str[g_editor.mode];

    mvprintw(0, 1,  "BashBeats");
    mvprintw(0, 12, "| MODE:%-5s |", mode_label);
    mvprintw(0, 28, "BPM:%-3d |", p->bpm);
    mvprintw(0, 38, "Track:%d/%-2d |", g_editor.cur_track + 1, p->track_count);
    mvprintw(0, 52, "Tick:%-4d |", g_editor.cur_tick);
    mvprintw(0, 63, "Play:%s |",
             audio_is_playing() ? (audio_is_paused() ? "PAUSED" : "PLAY  ") : "STOP  ");
    mvprintw(0, 75, "Clients:%-2d", stream_clients());
    attroff(COLOR_PAIR(8) | A_BOLD);

    /* Title row */
    attron(COLOR_PAIR(1));
    mvprintw(1, 1, "Project: %s", p->title);
    attroff(COLOR_PAIR(1));
    mvhline(2, 0, ACS_HLINE, COLS);
}

/* ── Help bar (nano-style, context-sensitive) ── */
void editor_draw_help(void)
{
    int help_row = LINES - HELP_ROWS;
    attron(A_REVERSE);
    for (int c = 0; c < COLS; c++) mvaddch(help_row, c, ' ');

    switch (g_editor.mode) {
    case MODE_TRACK:
        mvprintw(help_row, 0,
            "^T Tracks  ^E Edit(sel)  ^P Piano  ^F File  ^Q Quit  "
            "Up/Down:Select  m:Mute  +/-:Vol  a:Add  d:Del  Space:Play/Stop");
        break;
    case MODE_EDIT:
        mvprintw(help_row, 0,
            "^T Tracks  ^E Edit  ^P Piano  ^F File  ^Q Quit  "
            "Arrows:Move  SPC:Note  ,:Start  .:End  DEL:Del  +/-:BPM");
        break;
    case MODE_PLAY:
        mvprintw(help_row, 0,
            "^E Edit  ^P Play/Pause  SPC:Play/Pause  "
            "+/-:BPM  ^F File  ^Q Quit");
        break;
    case MODE_FILE:
        mvprintw(help_row, 0,
            "FILE  S:Save  L:Load  N:New project  R:Rename path  T:Title  ESC:Back");
        break;
    case MODE_PIANO:
        mvprintw(help_row, 0,
            "^P Close Piano  a-j:Oct1 White  2357:Oct1 Black  "
            "q-u:Oct2 White  !@$%%^:Oct2 Black");
        break;
    }
    attroff(A_REVERSE);

    /* Status row just above help */
    attron(COLOR_PAIR(3));
    mvprintw(help_row + 1, 0, " %s", g_editor.status_msg);
    attroff(COLOR_PAIR(3));
}

/* ═══════════════════════════════════════════════
 *  TRACK MODE DRAW
 * ═══════════════════════════════════════════════ */

/* Volume bar: 0.0~1.0 -> 10-char bar using ACS_BLOCK / '-' */
static void draw_vol_bar(int row, int col, float vol)
{
    int filled = (int)(vol * 10.0f + 0.5f);
    if (filled > 10) filled = 10;
    for (int i = 0; i < 10; i++) {
        if (i < filled) { attron(COLOR_PAIR(2)); addch(ACS_BLOCK); attrset(A_NORMAL); }
        else             addch('-');
    }
    (void)row; (void)col;
}

void editor_draw_track(const Project *p)
{
    int r = HEADER_ROWS;
    int body_rows = LINES - HEADER_ROWS - HELP_ROWS - STATUS_ROW;

    /* ── Column header ── */
    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(r, 0, ' ', COLS);
    mvprintw(r, 0, " %-2s  %-12s  %-4s  %-10s  %-5s  %-20s  %s",
             "#", "Name", "Mute", "Volume", "Notes", "Instrument", "Base");
    attrset(A_NORMAL);
    r++;
    mvhline(r, 0, ACS_HLINE, COLS);
    r++;

    /* ── Track rows ── */
    for (int t = 0; t < p->track_count && r < HEADER_ROWS + body_rows; t++, r++) {
        int is_sel = (t == g_editor.track_cursor);

        if (is_sel) attron(COLOR_PAIR(3) | A_BOLD);
        else        attron(COLOR_PAIR(6));

        mvprintw(r, 0, "%c%-2d  ", is_sel ? '>' : ' ', t + 1);
        mvprintw(r, 5, "%-12.12s  ", p->tracks[t].name);

        /* Mute */
        if (p->tracks[t].mute) {
            attrset(A_NORMAL); attron(COLOR_PAIR(4) | A_BOLD);
            mvprintw(r, 20, "[MUTE]");
            attrset(A_NORMAL);
            if (is_sel) attron(COLOR_PAIR(3) | A_BOLD);
            else        attron(COLOR_PAIR(6));
        } else {
            mvprintw(r, 20, "      ");
        }

        /* Volume bar */
        attrset(A_NORMAL);
        move(r, 28);
        draw_vol_bar(r, 28, p->tracks[t].volume);
        mvprintw(r, 39, " %3d%% ", (int)(p->tracks[t].volume * 100.0f + 0.5f));

        /* Note count */
        if (is_sel) attron(COLOR_PAIR(3) | A_BOLD);
        else        attron(COLOR_PAIR(6));
        mvprintw(r, 45, "%-5d  ", p->tracks[t].event_count);

        /* Instrument name (basename of path) */
        const char *instr_path = p->tracks[t].instrument;
        const char *slash = strrchr(instr_path, '/');
        const char *instr_name = slash ? slash + 1 : instr_path;
        if (instr_name[0] == '\0') instr_name = "(none)";
        attrset(A_NORMAL);
        attron(COLOR_PAIR(5));
        mvprintw(r, 52, "%-20.20s  ", instr_name);
        attrset(A_NORMAL);

        /* Base note */
        if (is_sel) attron(COLOR_PAIR(3) | A_BOLD);
        else        attron(COLOR_PAIR(6));
        mvprintw(r, 74, "%-4s", midi_note_name(p->tracks[t].base_note));

        /* Editing tag */
        if (t == g_editor.cur_track) {
            attrset(A_NORMAL); attron(COLOR_PAIR(9) | A_BOLD);
            mvprintw(r, 79, "[edit]");
        }
        attrset(A_NORMAL);
    }

    /* ── Instructions ── */
    int sep = HEADER_ROWS + 2 + p->track_count + 1;
    if (sep < LINES - HELP_ROWS - STATUS_ROW - 2) {
        mvhline(sep, 0, ACS_HLINE, COLS);
        attron(COLOR_PAIR(1));
        mvprintw(sep + 1, 2,
            "Up/Down:select  m:mute  +/-:volume  a:add track  d:delete  "
            "i:change instrument  Space:play/stop");
        mvprintw(sep + 2, 2,
            "Ctrl+E: open pianoroll for selected track   Ctrl+S: save project");
        attrset(A_NORMAL);
    }
}

/* ── Track sidebar ── */
static void draw_track_sidebar(const Project *p)
{
    int row_start = HEADER_ROWS;
    int rows_avail = LINES - HEADER_ROWS - HELP_ROWS - STATUS_ROW;

    /* Track list at top of sidebar (one line per track) */
    for (int t = 0; t < p->track_count && t < rows_avail; t++) {
        if (t == g_editor.cur_track) attron(COLOR_PAIR(3) | A_BOLD);
        else                          attron(COLOR_PAIR(7));

        char vol_bar[5];
        int vbars = (int)(p->tracks[t].volume * 4.0f + 0.5f);
        for (int i = 0; i < 4; i++) vol_bar[i] = (i < vbars) ? '#' : '-';
        vol_bar[4] = '\0';

        mvprintw(row_start + t, 0, "%c%-6.6s%s",
                 p->tracks[t].mute ? 'M' : ' ',
                 p->tracks[t].name,
                 vol_bar);
        attrset(A_NORMAL);
    }

    /* Divider */
    for (int r = row_start; r < LINES - HELP_ROWS - STATUS_ROW; r++)
        mvaddch(r, TRACK_COLS - 1, ACS_VLINE);
}

/* ── note_shape_at ──
 * Returns the 3-char string to draw for a note cell, encoding the note's
 * position within its event (start / middle / end / single) and whether
 * an adjacent tick belongs to a *different* note event (so boundaries are
 * rendered as ][  instead of ## ).
 *
 * left_char  : leftmost character  (index 0)
 * mid_char   : middle character    (index 1)
 * right_char : rightmost character (index 2)
 *
 * All three are written into out[3]; caller appends '\0' if needed.
 */
static void note_shape_at(const Project *p, int track,
                           uint32_t tick, int note,
                           char out[3])
{
    /* Determine whether adjacent ticks also belong to a note */
    int cur  = editor_note_at(p, track, tick, note);

    if (!cur) {
        out[0] = out[1] = out[2] = 0; /* caller should not call this when !cur */
        return;
    }

    /* Check if the previous tick belongs to the SAME event as this tick
     * (not just any note at that pitch).  We need this to distinguish
     * two back-to-back separate notes from one long note.             */
    int same_prev = 0, same_next = 0;
    const Track *t = &p->tracks[track];
    for (int i = 0; i < t->event_count; i++) {
        const NoteEvent *ev = &t->events[i];
        if (ev->note != (uint8_t)note) continue;
        if (tick >= ev->start_tick && tick < ev->start_tick + ev->duration_tick) {
            /* This is the event that owns 'tick' */
            if (tick > ev->start_tick)                          same_prev = 1;
            if (tick + 1 < ev->start_tick + ev->duration_tick) same_next = 1;
            break;
        }
    }

    /*
     * Encoding rules (TICK_COL_WIDTH == 3):
     *
     *  single note  (1 tick)           : [#]
     *  note start   (continues right)  : [##   → left='[' mid='#' right='#'
     *  note middle  (continues both)   : ###
     *  note end     (continues left)   : ##]
     *
     *  boundary between two separate notes at the same pitch:
     *    right edge of left note  : ##]  (same_next==0, prev note exists)
     *    left  edge of right note : [##  (same_prev==0, next note exists)
     *    — but when they are adjacent the right ']' and left '[' would
     *      overlap in the same column; we render the right-edge cell as
     *      "##]" and the left-edge of the next as "[##", so between them
     *      you see "##][##" which clearly shows the boundary.
     */

    char L = same_prev ? '#' : '[';
    char R = same_next ? '#' : ']';
    char M = '#';

    out[0] = L;
    out[1] = M;
    out[2] = R;
}

/* ── Pianoroll grid ── */
void editor_draw_pianoroll(const Project *p)
{
    int row_start  = HEADER_ROWS;
    int col_start  = TRACK_COLS + NOTE_LABEL_COLS;
    int rows_avail = LINES - HEADER_ROWS - HELP_ROWS - STATUS_ROW;
    int vcols      = visible_tick_cols();

    /* Per-track note range: base_note-12 .. base_note+11 (2 octaves) */
    const Track *ct   = &p->tracks[g_editor.cur_track];
    int note_min = TRACK_NOTE_MIN(ct);
    int note_max = TRACK_NOTE_MAX(ct);
    if (note_min < 0)   note_min = 0;
    if (note_max > 127) note_max = 127;

    /* Clamp cursor note to current range */
    if (g_editor.cur_note < note_min) g_editor.cur_note = note_min;
    if (g_editor.cur_note > note_max) g_editor.cur_note = note_max;

    /* Clamp view */
    if (g_editor.cur_tick < g_editor.view_tick_start)
        g_editor.view_tick_start = g_editor.cur_tick;
    if (g_editor.cur_tick >= g_editor.view_tick_start + vcols)
        g_editor.view_tick_start = g_editor.cur_tick - vcols + 1;
    g_editor.view_tick_cols = vcols;

    /* ── Tick ruler ── */
    for (int tc = 0; tc < vcols; tc++) {
        int tick = g_editor.view_tick_start + tc;
        int col  = col_start + tc * TICK_COL_WIDTH;
        if (tick % TICKS_PER_QN == 0) {
            attron(COLOR_PAIR(1));
            mvprintw(row_start, col, "%-3d", tick / TICKS_PER_QN + 1);
            attrset(A_NORMAL);
        } else {
            mvprintw(row_start, col, ".  ");
        }
    }

    /* ── Piano rows: note_max at top, note_min at bottom ── */
    int piano_rows = note_max - note_min + 1;  /* == 24 */
    int display_rows = (piano_rows < rows_avail - 1) ? piano_rows : rows_avail - 1;

    for (int ri = 0; ri < display_rows; ri++) {
        int note = note_max - ri;
        int row  = row_start + 1 + ri;
        if (row >= LINES - HELP_ROWS - STATUS_ROW) break;

        /* Is this note a sharp/black key? (chroma 1,3,6,8,10) */
        int chroma  = note % 12;
        int is_sharp = (chroma==1||chroma==3||chroma==6||chroma==8||chroma==10);
        if (is_sharp) attron(A_DIM);
        mvprintw(row, TRACK_COLS, "%-4s", midi_note_name(note));
        attrset(A_NORMAL);

        /* ── Tick cells ── */
        for (int tc = 0; tc < vcols; tc++) {
            int tick     = g_editor.view_tick_start + tc;
            int col      = col_start + tc * TICK_COL_WIDTH;
            int has_note = editor_note_at(p, g_editor.cur_track, tick, note);
            int is_cursor= (tick == g_editor.cur_tick && note == g_editor.cur_note);

            int in_span = 0;
            if (g_editor.span_active && note == g_editor.span_note) {
                int s = g_editor.span_start_tick;
                int e = g_editor.cur_tick;
                if (s > e) { int tmp = s; s = e; e = tmp; }
                in_span = (tick >= s && tick <= e);
            }

            if      (is_cursor && has_note) attron(COLOR_PAIR(9) | A_BOLD);
            else if (is_cursor)             attron(COLOR_PAIR(3) | A_BOLD);
            else if (in_span)               attron(COLOR_PAIR(10));
            else if (has_note)              attron(COLOR_PAIR(2));
            else if (tick % TICKS_PER_QN == 0) attron(A_DIM);

            /* ── Draw exactly 3 characters ── */
            if (has_note) {
                char shape[3];
                note_shape_at(p, g_editor.cur_track, (uint32_t)tick, note, shape);
                /* mvaddch x3 avoids any printf format/width surprises */
                mvaddch(row, col,     shape[0]);
                mvaddch(row, col + 1, shape[1]);
                mvaddch(row, col + 2, shape[2]);
            } else if (is_cursor) {
                mvaddstr(row, col, "[ ]");
            } else if (in_span) {
                mvaddstr(row, col, "[~]");
            } else if (tick % TICKS_PER_QN == 0) {
                mvaddstr(row, col, "|  ");
            } else {
                mvaddstr(row, col, ".  ");
            }

            /* ── Always reset attrs cleanly ── */
            attrset(A_NORMAL);
        }
    }

    draw_track_sidebar(p);
}

/* ── PLAY mode view ── */
void editor_draw_play(const Project *p)
{
    int r = HEADER_ROWS + 1;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(r, 2, "── PLAY MODE ──");
    attroff(COLOR_PAIR(1) | A_BOLD);

    uint32_t tick = audio_current_tick();
    int bar  = tick / TICKS_PER_QN + 1;

    mvprintw(r+2, 2, "Status : %s",
             audio_is_paused()  ? "PAUSED" :
             audio_is_playing() ? "PLAYING" : "STOPPED");
    mvprintw(r+3, 2, "BPM    : %d", audio_get_bpm());
    mvprintw(r+4, 2, "Tick   : %u  (Bar %d)", tick, bar);
    mvprintw(r+5, 2, "Tracks : %d", p->track_count);
    mvprintw(r+6, 2, "Clients: %d", stream_clients());

    /* Simple playhead progress bar */
    mvprintw(r+8, 2, "Playhead: [");
    int bar_w = COLS - 18;
    int filled = (bar_w > 0 && p->track_count > 0) ?
                 (int)(((float)tick / 64.0f) * bar_w) : 0;
    if (filled > bar_w) filled = bar_w;
    for (int i = 0; i < bar_w; i++) {
        if (i < filled) { attron(COLOR_PAIR(2)); addch('='); attroff(COLOR_PAIR(2)); }
        else addch('-');
    }
    addch(']');
}

/* ── FILE mode view ── */
void editor_draw_file(const Project *p)
{
    int panel_w = 62;
    int panel_x = (COLS - panel_w) / 2;
    if (panel_x < 1) panel_x = 1;
    int r = HEADER_ROWS + 1;

    /* ── Title bar ── */
    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(r, panel_x, ' ', panel_w);
    mvprintw(r, panel_x + (panel_w - 11) / 2, " FILE MANAGER ");
    attroff(COLOR_PAIR(8) | A_BOLD);
    r++;

    /* ── Box top border ── */
    mvaddch(r, panel_x, ACS_ULCORNER);
    mvhline(r, panel_x + 1, ACS_HLINE, panel_w - 2);
    mvaddch(r, panel_x + panel_w - 1, ACS_URCORNER);
    r++;

    /* ── Project info section ── */
    /* Row: title */
    mvaddch(r, panel_x, ACS_VLINE);
    attron(A_BOLD);
    mvprintw(r, panel_x + 2, "  Title  ");
    attroff(A_BOLD);
    attron(COLOR_PAIR(3));
    mvprintw(r, panel_x + 11, "%-48.48s", p->title);
    attroff(COLOR_PAIR(3));
    mvaddch(r, panel_x + panel_w - 1, ACS_VLINE);
    r++;

    /* Row: path */
    mvaddch(r, panel_x, ACS_VLINE);
    attron(A_BOLD);
    mvprintw(r, panel_x + 2, "  Path   ");
    attroff(A_BOLD);
    /* Truncate path from left if too long */
    const char *fp = g_editor.file_path;
    int fp_len = (int)strlen(fp);
    int fp_avail = panel_w - 14;
    if (fp_len > fp_avail)
        fp = fp + (fp_len - fp_avail);
    mvprintw(r, panel_x + 11, "%-48.48s", fp);
    mvaddch(r, panel_x + panel_w - 1, ACS_VLINE);
    r++;

    /* Row: stats */
    mvaddch(r, panel_x, ACS_VLINE);
    attron(A_BOLD);
    mvprintw(r, panel_x + 2, "  Stats  ");
    attroff(A_BOLD);

    /* count total events */
    int total_ev = 0;
    for (int i = 0; i < p->track_count; i++)
        total_ev += p->tracks[i].event_count;
    mvprintw(r, panel_x + 11, "%d track%s   %d note%s   %d BPM",
             p->track_count, p->track_count != 1 ? "s" : "",
             total_ev,       total_ev != 1 ? "s" : "",
             p->bpm);
    /* fill rest */
    int used = 11 + 40;
    for (int i = used; i < panel_w - 2; i++)
        mvaddch(r, panel_x + i, ' ');
    mvaddch(r, panel_x + panel_w - 1, ACS_VLINE);
    r++;

    /* Divider */
    mvaddch(r, panel_x, ACS_LTEE);
    mvhline(r, panel_x + 1, ACS_HLINE, panel_w - 2);
    mvaddch(r, panel_x + panel_w - 1, ACS_RTEE);
    r++;

    /* ── Actions section header ── */
    mvaddch(r, panel_x, ACS_VLINE);
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(r, panel_x + 2, " %-*s", panel_w - 5, "ACTIONS");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvaddch(r, panel_x + panel_w - 1, ACS_VLINE);
    r++;

    /* ── Action rows ── */
    /* helper macro: draw one action row */
    #define ACT_ROW(key_str, label, desc) do {                          \
        mvaddch(r, panel_x, ACS_VLINE);                                 \
        attron(COLOR_PAIR(3) | A_BOLD);                                 \
        mvprintw(r, panel_x + 3, " %-5s", key_str);                    \
        attroff(COLOR_PAIR(3) | A_BOLD);                                \
        attron(A_BOLD);                                                  \
        mvprintw(r, panel_x + 9, "%-14s", label);                      \
        attroff(A_BOLD);                                                 \
        mvprintw(r, panel_x + 23, "%-38.38s", desc);                   \
        mvaddch(r, panel_x + panel_w - 1, ACS_VLINE);                  \
        r++;                                                             \
    } while(0)

    ACT_ROW("S",     "Save",          "Save project to current path");
    ACT_ROW("L",     "Load",          "Load project from current path");
    ACT_ROW("N",     "New project",   "Create new project (unsaved data lost)");
    ACT_ROW("R",     "Rename path",   "Change save/load file path");
    ACT_ROW("T",     "Set title",     "Rename this project");
    ACT_ROW("ESC",   "Back",          "Return to previous mode");
    #undef ACT_ROW

    /* ── Box bottom border ── */
    mvaddch(r, panel_x, ACS_LLCORNER);
    mvhline(r, panel_x + 1, ACS_HLINE, panel_w - 2);
    mvaddch(r, panel_x + panel_w - 1, ACS_LRCORNER);
}

/* ── Master draw ── */
static void draw_all(const Project *p)
{
    erase();
    editor_draw_header(p);

    switch (g_editor.mode) {
    case MODE_TRACK:
        editor_draw_track(p);
        break;
    case MODE_EDIT:
        editor_draw_pianoroll(p);
        break;
    case MODE_PIANO:
        editor_draw_pianoroll(p);
        piano_draw(HEADER_ROWS + 1, 2, g_editor.cur_track);
        break;
    case MODE_PLAY:
        editor_draw_play(p);
        break;
    case MODE_FILE:
        editor_draw_file(p);
        break;
    }

    editor_draw_help();
    refresh();
}

/* ═══════════════════════════════════════════════
 *  NOTE OPERATIONS
 * ═══════════════════════════════════════════════ */

/* Returns 1 if the given track has a note that covers tick/note */
int editor_note_at(const Project *p, int track, uint32_t tick, int note)
{
    if (track < 0 || track >= p->track_count) return 0;
    const Track *t = &p->tracks[track];
    for (int i = 0; i < t->event_count; i++) {
        const NoteEvent *ev = &t->events[i];
        if (ev->note == (uint8_t)note &&
            tick >= ev->start_tick &&
            tick <  ev->start_tick + ev->duration_tick)
            return 1;
    }
    return 0;
}

void editor_place_note(Project *p, uint32_t start, uint32_t dur, int note, uint8_t vel)
{
    pthread_mutex_lock(&g_project_mtx);
    Track *t = &p->tracks[g_editor.cur_track];
    if (t->event_count >= MAX_EVENTS) {
        pthread_mutex_unlock(&g_project_mtx);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track full! MAX_EVENTS=%d reached.", MAX_EVENTS);
        return;
    }
    NoteEvent *ev   = &t->events[t->event_count++];
    ev->start_tick    = start;
    ev->duration_tick = dur > 0 ? dur : 1;
    ev->note          = (uint8_t)note;
    ev->velocity      = vel;
    pthread_mutex_unlock(&g_project_mtx);

    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "Note placed: %s tick=%u dur=%u vel=%u",
             midi_note_name(note), start, dur, (unsigned)vel);
}

void editor_delete_note(Project *p, uint32_t tick, int note)
{
    pthread_mutex_lock(&g_project_mtx);
    Track *t = &p->tracks[g_editor.cur_track];
    for (int i = 0; i < t->event_count; i++) {
        NoteEvent *ev = &t->events[i];
        if (ev->note == (uint8_t)note &&
            tick >= ev->start_tick &&
            tick <  ev->start_tick + ev->duration_tick) {
            t->events[i] = t->events[--t->event_count];
            pthread_mutex_unlock(&g_project_mtx);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Note deleted: %s at tick=%u", midi_note_name(note), tick);
            return;
        }
    }
    pthread_mutex_unlock(&g_project_mtx);
    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "No note at %s tick=%u", midi_note_name(note), tick);
}

/* ═══════════════════════════════════════════════
 *  MODE TRANSITION
 * ═══════════════════════════════════════════════ */

void editor_set_mode(EditorMode m, Project *p)
{
    /* Cancel any active span when switching modes */
    g_editor.span_active = 0;
    g_editor.mode = m;

    switch (m) {
    case MODE_TRACK:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "TRACK mode: Up/Down=select, m=mute, +/-=vol, Ctrl+E=edit track");
        break;
    case MODE_PLAY:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "PLAY mode: Space=play/pause, +/-=BPM");
        break;
    case MODE_EDIT:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "EDIT mode: arrows=move, SPC=note, ,=start .=end, DEL=delete");
        break;
    case MODE_FILE:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "S=Save  L=Load  N=New  R=Path  T=Title  ESC=Back");
        break;
    case MODE_PIANO:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "PIANO mode: play keys, ^P to close");
        break;
    }
    (void)p;
}

/* ═══════════════════════════════════════════════
 *  TRACK MODE KEY HANDLER
 * ═══════════════════════════════════════════════ */

static void handle_track_key(int ch, Project *p)
{
    switch (ch) {
    /* ── Cursor navigation ── */
    case KEY_UP:
        if (g_editor.track_cursor > 0) {
            g_editor.track_cursor--;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d: %s", g_editor.track_cursor + 1,
                     p->tracks[g_editor.track_cursor].name);
        }
        break;
    case KEY_DOWN:
        if (g_editor.track_cursor < p->track_count - 1) {
            g_editor.track_cursor++;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d: %s", g_editor.track_cursor + 1,
                     p->tracks[g_editor.track_cursor].name);
        }
        break;

    /* ── Mute toggle ── */
    case 'm':
    case 'M':
        p->tracks[g_editor.track_cursor].mute ^= 1;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d %s", g_editor.track_cursor + 1,
                 p->tracks[g_editor.track_cursor].mute ? "MUTED" : "unmuted");
        break;

    /* ── Volume adjust ── */
    case '+':
    case '=': {
        float v = p->tracks[g_editor.track_cursor].volume + 0.05f;
        if (v > 1.0f) v = 1.0f;
        p->tracks[g_editor.track_cursor].volume = v;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d volume: %d%%", g_editor.track_cursor + 1, (int)(v * 100));
        break;
    }
    case '-': {
        float v = p->tracks[g_editor.track_cursor].volume - 0.05f;
        if (v < 0.0f) v = 0.0f;
        p->tracks[g_editor.track_cursor].volume = v;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d volume: %d%%", g_editor.track_cursor + 1, (int)(v * 100));
        break;
    }

    /* ── Add track (requires instrument selection) ── */
    case 'a':
    case 'A':
        if (p->track_count >= MAX_TRACKS) {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Max tracks (%d) reached.", MAX_TRACKS);
            break;
        }
        {
            char instr[128] = {0};
            if (!pick_instrument(instr)) {
                snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                         "Track creation cancelled.");
                break;
            }
            int idx = p->track_count++;
            snprintf(p->tracks[idx].name, 31, "Track%d", idx + 1);
            strncpy(p->tracks[idx].instrument, instr, 127);
            p->tracks[idx].volume      = 1.0f;
            p->tracks[idx].mute        = 0;
            p->tracks[idx].event_count = 0;
            p->tracks[idx].base_note   = 60;  /* default: middle C */
            g_editor.track_cursor = idx;
            audio_load_instrument(idx, instr);
            const char *nm = strrchr(instr, '/');
            nm = nm ? nm + 1 : instr;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d added (%.40s).", idx + 1, nm);
        }
        break;

    /* ── Change instrument for selected track ── */
    case 'i':
    case 'I': {
        char instr[128] = {0};
        if (pick_instrument(instr)) {
            strncpy(p->tracks[g_editor.track_cursor].instrument, instr, 127);
            audio_load_instrument(g_editor.track_cursor, instr);
            const char *nm2 = strrchr(instr, '/');
            nm2 = nm2 ? nm2 + 1 : instr;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d instrument -> %.40s", g_editor.track_cursor + 1, nm2);
        }
        break;
    }

    /* ── Delete track ── */
    case 'd':
    case 'D':
        if (p->track_count > 1) {
            int idx = g_editor.track_cursor;
            for (int i = idx; i < p->track_count - 1; i++)
                p->tracks[i] = p->tracks[i + 1];
            p->track_count--;
            if (g_editor.track_cursor >= p->track_count)
                g_editor.track_cursor = p->track_count - 1;
            if (g_editor.cur_track >= p->track_count)
                g_editor.cur_track = p->track_count - 1;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d deleted.", idx + 1);
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Cannot delete last track.");
        }
        break;

    /* ── Play / stop ── */
    case ' ':
        if (audio_is_playing()) {
            audio_stop();
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Stopped.");
        } else {
            audio_play(p);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Playing: %s", p->title);
        }
        break;

    default:
        break;
    }
}

/* ═══════════════════════════════════════════════
 *  KEY HANDLERS
 * ═══════════════════════════════════════════════ */

static void handle_edit_key(int ch, Project *p)
{
    int note  = g_editor.cur_note;
    int tick  = g_editor.cur_tick;

    switch (ch) {
    /* ── Cursor movement ── */
    case KEY_UP: {
        int nmax = TRACK_NOTE_MAX(&p->tracks[g_editor.cur_track]);
        if (nmax > 127) nmax = 127;
        if (g_editor.cur_note < nmax) g_editor.cur_note++;
        break;
    }
    case KEY_DOWN: {
        int nmin = TRACK_NOTE_MIN(&p->tracks[g_editor.cur_track]);
        if (nmin < 0) nmin = 0;
        if (g_editor.cur_note > nmin) g_editor.cur_note--;
        break;
    }
    case KEY_RIGHT:
        g_editor.cur_tick++;
        break;
    case KEY_LEFT:
        if (g_editor.cur_tick > 0) g_editor.cur_tick--;
        break;

    /* ── Single-tick note (spacebar) ── */
    case ' ':
        if (editor_note_at(p, g_editor.cur_track, tick, note))
            editor_delete_note(p, tick, note);
        else
            editor_place_note(p, tick, 1, note, 80);
        break;

    /* ── Span start ── */
    case ',':
        if (!g_editor.span_active) {
            g_editor.span_active     = 1;
            g_editor.span_start_tick = tick;
            g_editor.span_note       = note;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Span started at tick=%d note=%s — move right, press '.' to finish",
                     tick, midi_note_name(note));
        }
        break;

    /* ── Span end ── */
    case '.':
        if (g_editor.span_active) {
            int s    = g_editor.span_start_tick;
            int e    = tick;
            int snote = g_editor.span_note;
            if (s > e) { int tmp = s; s = e; e = tmp; }
            uint32_t dur = (uint32_t)(e - s + 1);
            editor_place_note(p, (uint32_t)s, dur, snote, 80);
            g_editor.span_active = 0;
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "No span active. Press ',' first to start a span.");
        }
        break;

    /* ── Delete note under cursor ── */
    case KEY_DC:  /* Delete key */
    case 127:     /* Backspace on some terminals */
        editor_delete_note(p, tick, note);
        break;

    /* ── BPM adjust ── */
    case '+':
    case '=': {
        int bpm = audio_get_bpm() + 5;
        if (bpm > 300) bpm = 300;
        audio_set_bpm(bpm);
        p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "BPM → %d", bpm);
        break;
    }
    case '-': {
        int bpm = audio_get_bpm() - 5;
        if (bpm < 20) bpm = 20;
        audio_set_bpm(bpm);
        p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "BPM → %d", bpm);
        break;
    }

    /* ── Track selection ── */
    case '[':
        if (g_editor.cur_track > 0) {
            g_editor.cur_track--;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track → %d: %s", g_editor.cur_track,
                     p->tracks[g_editor.cur_track].name);
        }
        break;
    case ']':
        if (g_editor.cur_track < p->track_count - 1) {
            g_editor.cur_track++;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track → %d: %s", g_editor.cur_track,
                     p->tracks[g_editor.cur_track].name);
        }
        break;

    default:
        break;
    }
}

static void handle_play_key(int ch, Project *p)
{
    switch (ch) {
    case ' ':
    case KEY_CTRL_P:
        if (audio_is_paused()) {
            audio_resume();
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Resumed.");
        } else if (audio_is_playing()) {
            audio_pause();
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Paused.");
        } else {
            audio_play(p);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Playing: %s", p->title);
        }
        break;
    case 's':
    case 'S':
        audio_stop();
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Stopped.");
        break;
    case '+':
    case '=': {
        int bpm = audio_get_bpm() + 5;
        if (bpm > 300) bpm = 300;
        audio_set_bpm(bpm);
        p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "BPM → %d", bpm);
        break;
    }
    case '-': {
        int bpm = audio_get_bpm() - 5;
        if (bpm < 20) bpm = 20;
        audio_set_bpm(bpm);
        p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "BPM → %d", bpm);
        break;
    }
    default:
        break;
    }
    (void)p;
}

/* ── pick_instrument ──────────────────────────────────────────────
 * Displays an inline instrument picker (temporarily overrides body area).
 * Returns 1 if user selected an instrument (path written to out),
 *         0 if user pressed Escape/q (cancel).
 * ─────────────────────────────────────────────────────────────── */
static int pick_instrument(char out[128])
{
    /* Scan samples/ directory */
    char files[64][128];
    int  nfiles = file_list_instruments(files, 64);

    if (nfiles == 0) {
        /* No instruments found — should not happen after ensure_stub */
        snprintf(out, 128, "%s/silent.wav", SAMPLES_DIR);
        return 1;
    }

    /* Temporarily block getch() */
    nodelay(stdscr, FALSE);

    int sel = 0;
    int done = 0;
    int accepted = 0;

    while (!done) {
        /* Draw picker over body area */
        int r = HEADER_ROWS + 1;
        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r - 1, 0, ' ', COLS);
        mvprintw(r - 1, 2, "SELECT INSTRUMENT  (Up/Down: move  Enter: select  Esc: cancel)");
        attrset(A_NORMAL);

        for (int i = 0; i < nfiles; i++) {
            const char *slash = strrchr(files[i], '/');
            const char *nm    = slash ? slash + 1 : files[i];
            if (i == sel) {
                attron(COLOR_PAIR(3) | A_BOLD);
                mvprintw(r + i, 4, "> %-40s", nm);
                attrset(A_NORMAL);
            } else {
                mvprintw(r + i, 4, "  %-40s", nm);
            }
        }
        refresh();

        int ch = getch();
        switch (ch) {
        case KEY_UP:
            if (sel > 0) sel--;
            break;
        case KEY_DOWN:
            if (sel < nfiles - 1) sel++;
            break;
        case '\n':
        case '\r':
        case KEY_ENTER:
            strncpy(out, files[sel], 127);
            out[127] = '\0';
            accepted = 1;
            done = 1;
            break;
        case 27:   /* Escape */
        case 'q':
            done = 1;
            break;
        default:
            break;
        }
    }

    nodelay(stdscr, TRUE);
    return accepted;
}

/* ── Inline string prompt (temporarily disables nodelay) ── */
static void prompt_string(const char *prompt, char *out, int maxlen)
{
    int r = LINES - HELP_ROWS - STATUS_ROW;
    nodelay(stdscr, FALSE);
    echo();
    attron(COLOR_PAIR(3));
    mvprintw(r, 0, "%-*s", COLS - 1, "");
    mvprintw(r, 0, "%s", prompt);
    attroff(COLOR_PAIR(3));
    refresh();
    getnstr(out, maxlen - 1);
    noecho();
    nodelay(stdscr, TRUE);
}

static void handle_file_key(int ch, Project **pp)
{
    Project *p = *pp;
    switch (ch) {

    /* ── ESC: back to previous mode ── */
    case 27:
        editor_set_mode(MODE_TRACK, p);
        break;

    /* ── S: Save ── */
    case 's':
    case 'S': {
        int ret = project_save(p, g_editor.file_path);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 ret == 0 ? "Saved: %s" : "Save FAILED: %s",
                 g_editor.file_path);
        break;
    }

    /* ── L: Load ── */
    case 'l':
    case 'L': {
        Project *loaded = project_load(g_editor.file_path);
        if (loaded) {
            project_free(p);
            *pp = loaded;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Loaded: %.80s", g_editor.file_path);
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Load FAILED: %.70s", g_editor.file_path);
        }
        break;
    }

    /* ── N: New project ── */
    case 'n':
    case 'N': {
        project_free(p);
        *pp = project_new(NULL);
        g_editor.cur_track  = 0;
        g_editor.cur_tick   = 0;
        g_editor.track_cursor = 0;
        file_default_path((*pp)->title, g_editor.file_path,
                          sizeof(g_editor.file_path));
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "New project created.");
        break;
    }

    /* ── R: Rename path ── */
    case 'r':
    case 'R': {
        char buf[256] = {0};
        prompt_string("Save path (.bbeat): ", buf, 255);
        if (buf[0]) strncpy(g_editor.file_path, buf, 255);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Path: %s", g_editor.file_path);
        break;
    }

    /* ── T: Set title ── */
    case 't':
    case 'T': {
        char buf[64] = {0};
        prompt_string("Title: ", buf, 63);
        if (buf[0]) {
            strncpy((*pp)->title, buf, 63);
            /* Update default path to match new title */
            file_default_path((*pp)->title, g_editor.file_path,
                              sizeof(g_editor.file_path));
        }
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Title: %s", (*pp)->title);
        break;
    }

    /* Ctrl+S still works (muscle memory) */
    case KEY_CTRL_S: {
        int ret = project_save(*pp, g_editor.file_path);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 ret == 0 ? "Saved: %s" : "Save FAILED: %s",
                 g_editor.file_path);
        break;
    }

    default:
        break;
    }
}

/* ═══════════════════════════════════════════════
 *  MAIN EVENT LOOP
 * ═══════════════════════════════════════════════ */

void editor_run(Project *p)
{
    while (1) {
        /* Refresh stream client count for header */
        g_editor.stream_clients = stream_clients();

        draw_all(p);

        int ch = getch();
        if (ch == ERR) {
            /* No key pressed — small sleep to avoid 100% CPU spin */
            usleep(16000); /* ~60 fps */
            continue;
        }

        /* ── Global keys (all modes) ── */
        if (ch == KEY_CTRL_Q) {
            if (audio_is_playing()) audio_stop();
            break;
        }
        if (ch == KEY_CTRL_T) { editor_set_mode(MODE_TRACK, p); continue; }
        if (ch == KEY_CTRL_F) { editor_set_mode(MODE_FILE,  p); continue; }

        /* Ctrl+E:
         *   In TRACK mode → enter EDIT for the currently selected track.
         *   In EDIT/PIANO/PLAY → go back to TRACK mode (breadcrumb).
         *   Re-pressing Ctrl+E in EDIT stays in EDIT (no-op).              */
        if (ch == KEY_CTRL_E) {
            if (g_editor.mode == MODE_TRACK) {
                g_editor.cur_track = g_editor.track_cursor;
                editor_set_mode(MODE_EDIT, p);
            } else if (g_editor.mode == MODE_EDIT) {
                /* already in edit — no-op, stay */
            } else {
                editor_set_mode(MODE_EDIT, p);
            }
            continue;
        }

        /* Ctrl+P: piano overlay in EDIT/TRACK; play-pause in PLAY */
        if (ch == KEY_CTRL_P) {
            if (g_editor.mode == MODE_EDIT || g_editor.mode == MODE_TRACK) {
                editor_set_mode(MODE_PIANO, p);
            } else if (g_editor.mode == MODE_PIANO) {
                editor_set_mode(MODE_EDIT, p);
            } else if (g_editor.mode == MODE_PLAY) {
                handle_play_key(ch, p);
            } else {
                editor_set_mode(MODE_PLAY, p);
            }
            continue;
        }

        /* ── Mode-specific dispatch ── */
        switch (g_editor.mode) {
        case MODE_TRACK:
            handle_track_key(ch, p);
            break;
        case MODE_EDIT:
            handle_edit_key(ch, p);
            break;
        case MODE_PIANO:
            if (piano_handle_key(ch, p, g_editor.cur_track) == -1)
                editor_set_mode(MODE_EDIT, p);
            break;
        case MODE_PLAY:
            handle_play_key(ch, p);
            break;
        case MODE_FILE:
            handle_file_key(ch, &p);
            break;
        }
    }
}
