#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "editor.h"
#include "audio.h"
#include "file_io.h"
#include "stream.h"
#include "input.h"
#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <signal.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

/* ── Fixed terminal size ── */
#define TERM_ROWS  35
#define TERM_COLS  140

/* ── Layout constants ── */
#define HEADER_ROWS     3
#define HELP_ROWS       3   /* 2 help + 1 status */
#define TRACK_COLS      11
#define NOTE_LABEL_COLS 5
#define TICK_COL_WIDTH  3

/* ── Global editor state ── */
EditorState g_editor;

/* ── Forward declarations ── */
static void draw_all         (const Project *p);
static void handle_track_key (int ch, Project *p);
static void handle_edit_key  (int ch, Project *p);
static void handle_file_key  (int ch, Project **pp);
static int  pick_instrument  (char out[128]);
static int  pick_base_note   (void);
static int  pick_transpose   (void);
static int  pick_savefile    (char out[256]);
static void prompt_string    (const char *prompt, char *out, int maxlen);

/* ═══════════════════════════════════════════════
 *  INIT / CLEANUP
 * ═══════════════════════════════════════════════ */

void editor_init(Project *p)
{
    /* Resize terminal window via xterm escape sequence before ncurses */
    printf("\033[8;%d;%dt", TERM_ROWS, TERM_COLS);
    fflush(stdout);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(1);

    /* Request fixed terminal size */
    resizeterm(TERM_ROWS, TERM_COLS);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1,  COLOR_CYAN,    -1);   /* header / labels */
        init_pair(2,  COLOR_GREEN,   -1);   /* note block */
        init_pair(3,  COLOR_YELLOW,  -1);   /* cursor / selection */
        init_pair(4,  COLOR_RED,     -1);   /* muted / error */
        init_pair(5,  COLOR_MAGENTA, -1);   /* piano / instrument */
        init_pair(6,  COLOR_WHITE,   -1);   /* normal text */
        init_pair(7,  COLOR_BLUE,    -1);   /* track sidebar */
        init_pair(8,  COLOR_BLACK,   COLOR_CYAN);   /* header bg */
        init_pair(9,  COLOR_BLACK,   COLOR_GREEN);  /* active note cursor */
        init_pair(10, COLOR_BLACK,   COLOR_YELLOW); /* span preview */
        init_pair(11, COLOR_BLACK,   COLOR_RED);    /* playhead */
    }

    freopen("/tmp/bashbeats.log", "a", stderr);

    memset(&g_editor, 0, sizeof(g_editor));
    g_editor.mode             = MODE_TRACK;
    g_editor.track_cursor     = 0;
    g_editor.cur_tick         = 0;
    g_editor.cur_note         = p->tracks[0].base_note;
    g_editor.cur_track        = 0;
    g_editor.view_tick_start  = 0;
    g_editor.view_note_top    = (p->tracks[0].base_note >= 0)
                                ? p->tracks[0].base_note + 11 : 59;
    g_editor.span_active      = 0;
    g_editor.play_cursor      = 0;
    g_editor.exit_to_intro    = 0;

    file_default_path(p->title, g_editor.file_path, sizeof(g_editor.file_path));
    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "BashBeats ready.  Enter=edit track  ^F=file  Ctrl+C=quit");
    (void)p;
}

void editor_cleanup(void)
{
    endwin();
}

/* ═══════════════════════════════════════════════
 *  DRAW HELPERS
 * ═══════════════════════════════════════════════ */

static int visible_tick_cols(void)
{
    int w = COLS - NOTE_LABEL_COLS;
    if (w < 1) w = 1;
    return w / TICK_COL_WIDTH;
}

/* Compute last tick that has any note across all tracks */
static uint32_t project_last_tick(const Project *p)
{
    uint32_t last = 16;
    for (int t = 0; t < p->track_count; t++)
        for (int e = 0; e < p->tracks[t].event_count; e++) {
            uint32_t end = p->tracks[t].events[e].start_tick
                         + p->tracks[t].events[e].duration_tick;
            if (end > last) last = end;
        }
    return last;
}

/* ── Header ── */
void editor_draw_header(const Project *p)
{
    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    mvprintw(0, 1, "BashBeats");
    mvprintw(0, 12, "| %-5s |", g_editor.mode == MODE_TRACK ? "TRACK" :
                                  g_editor.mode == MODE_EDIT  ? "EDIT " :
                                  g_editor.mode == MODE_FILE  ? "FILE " : "???? ");
    mvprintw(0, 24, "BPM:%-3d |", p->bpm);
    mvprintw(0, 34, "Trk:%d/%-2d |", g_editor.cur_track+1, p->track_count);
    mvprintw(0, 47, "Tick:%-4u |", g_editor.cur_tick);
    mvprintw(0, 58, "%s",
             audio_is_playing() ? "[PLAYING]" :
             audio_is_paused()  ? "[PAUSED] " : "[STOPPED]");
    mvprintw(0, 69, "| Cli:%-2d", stream_clients());
    if (g_editor.mode == MODE_EDIT)
        mvprintw(0, 79, "| %.30s", p->tracks[g_editor.cur_track].name);
    attroff(COLOR_PAIR(8) | A_BOLD);

    attron(COLOR_PAIR(1));
    mvprintw(1, 1, "%s", p->title);
    attroff(COLOR_PAIR(1));
    mvhline(2, 0, ACS_HLINE, COLS);
}

/* ── Help bar ── */
void editor_draw_help(void)
{
    int hr = LINES - HELP_ROWS;   /* row for first help line */
    const char *help1 = "";
    const char *help2 = "";

    attron(A_REVERSE);
    mvhline(hr,     0, ' ', COLS);
    mvhline(hr + 1, 0, ' ', COLS);
    attroff(A_REVERSE);

    switch (g_editor.mode) {
    case MODE_TRACK:
        help1 = "Track  Enter:edit  Space:play/pause  ESC:intro  ^F:file  Ctrl+C:quit";
        help2 = "       Up/Dn:track  L/R:seek  0:start  a:add  c:dup  d:del  n:rename  i:instr  b:base  m:mute  ,/.BPM  +/-vol";
        break;
    case MODE_EDIT:
        help1 = "Edit  Arrows:move  Space:play/pause  Enter:note  ESC:tracks";
        help2 = "      , then .:long note  Del:erase  [/]:track  +/-:BPM  0:start  ^F:file  Ctrl+C:quit";
        break;
    case MODE_FILE:
        help1 = "File  S:save  L:load  N:new  R:path  T:title  E:export wav  ESC:back  Ctrl+C:quit";
        break;
    default:
        break;
    }

    attron(A_REVERSE);
    mvprintw(hr,     0, "%-*.*s", COLS - 1, COLS - 1, help1);
    mvprintw(hr + 1, 0, "%-*.*s", COLS - 1, COLS - 1, help2);
    attroff(A_REVERSE);

    /* Status line */
    attron(COLOR_PAIR(3));
    mvprintw(hr + 2, 0, " %-*.*s", COLS-2, COLS-2, g_editor.status_msg);
    attroff(COLOR_PAIR(3));
}



/* ── Progress bar (shared by TRACK and EDIT) ── */
static void draw_progress_bar(const Project *p, int row)
{
    if (row >= LINES - HELP_ROWS) return;

    uint32_t total  = project_last_tick(p);
    uint32_t cur    = (audio_is_playing() || audio_is_paused())
                    ? audio_current_tick()
                    : g_editor.play_cursor;
    if (cur > total) cur = total;

    int bar_w = COLS - 22;
    if (bar_w < 4) bar_w = 4;
    int filled = (total > 0) ? (int)((double)cur / total * bar_w) : 0;
    if (filled > bar_w) filled = bar_w;

    /* Sync play_cursor when playing */
    if (audio_is_playing()) g_editor.play_cursor = cur;

    mvhline(row, 0, ' ', COLS);
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(row, 1, "[");
    attroff(COLOR_PAIR(1) | A_BOLD);
    for (int i = 0; i < bar_w; i++) {
        if (i == filled) {
            attron(COLOR_PAIR(11) | A_BOLD); addch('|'); attrset(A_NORMAL);
        } else if (i < filled) {
            attron(COLOR_PAIR(2)); addch('='); attrset(A_NORMAL);
        } else {
            addch('-');
        }
    }
    attron(COLOR_PAIR(1) | A_BOLD);
    printw("]");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(row, bar_w + 3,
             " Tick:%-4u  Bar:%-3u  %s",
             cur, cur / TICKS_PER_QN + 1,
             audio_is_playing() ? "PLAY" : audio_is_paused() ? "PAUS" : "STOP");
}

/* ═══════════════════════════════════════════════
 *  TRACK MODE DRAW, edited space logic
 * ═══════════════════════════════════════════════ */

void editor_draw_track(const Project *p)
{
    int r = HEADER_ROWS;
    int body_end = LINES - HELP_ROWS - 2;

    /* ── Column positions (fixed for TERM_COLS=140) ──
     *   0   sel+#    6 chars
     *   6   Name    16 chars
     *  22   Mute     7 chars   "[MUTE] " or "       "
     *  29   Vol bar  8 chars   "########"
     *  37   Vol pct  6 chars   " 100%  "
     *  43   Notes    7 chars   "1234   "
     *  50   Base     7 chars   "C4     "
     *  57   Instr   35 chars
     *  92   [EDIT]
     */
    #define C_SEL   0
    #define C_NAME  6
    #define C_MUTE  22
    #define C_VOL   29
    #define C_VPCT  37    /* vol percent, separate from bar */
    #define C_NOTE  43
    #define C_BASE  50
    #define C_INST  57
    #define C_EDIT  92

    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(r, 0, ' ', COLS);
    mvprintw(r, 1, " #");          /* # at col 2 aligns with right-aligned track numbers */
    mvprintw(r, C_NAME, "%-16s", "Name");
    mvprintw(r, C_MUTE, "%-7s",  "Mute");
    mvprintw(r, C_VOL,  "%-8s",  "Volume");
    mvprintw(r, C_VPCT, "%-6s",  "    %");
    mvprintw(r, C_NOTE, "%-7s",  "Notes");
    mvprintw(r, C_BASE, "%-7s",  "Base");
    mvprintw(r, C_INST, "%-35s", "Instrument");
    attrset(A_NORMAL);
    r++;
    mvhline(r, 0, ACS_HLINE, COLS);
    r++;



    /* 수동 버퍼 매핑 방식으로 변경  */
    for (int t = 0; t < p->track_count && r <= body_end; t++, r++) {
        int is_sel = (t == g_editor.track_cursor);

        /* TERM_COLS(140) 크기의 고정 줄 버퍼 생성 및 공백 초기화 */
        char row[TERM_COLS + 1];
        memset(row, ' ', TERM_COLS);
        row[TERM_COLS] = '\0';

        /* 1. Col 0: 선택자 (Selector) */
        row[C_SEL] = is_sel ? '>' : ' ';

        /* 2. Cols 1-2: 우측 정렬 트랙 번호 */
        {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%2d", t + 1);
            row[1] = tmp[0]; 
            row[2] = tmp[1];
        }

        /* 3. Cols 6-21: 트랙 이름 (16자 고정폭 보장, 오버런 방지) */
        /* 3. Cols 6-21: 트랙 이름 (16자 고정폭 보장, 배열 검사 조건 수정) */
        if (p->tracks[t].name[0] != '\0') {  // [수정] 주소 체크 대신 첫 글자가 비어있는지 확인
            const char *nm = p->tracks[t].name;
            int nl = (int)strlen(nm);
            for (int i = 0; i < 16; i++) {
                row[C_NAME + i] = (i < nl && nm[i] != '\0') ? nm[i] : ' ';
            }
        }

        /* 4. Cols 22-27: 뮤트 상태 ([MUTE] 6자 정확히 대입) */
        if (p->tracks[t].mute) {
            memcpy(row + C_MUTE, "[MUTE]", 6);
        }

        /* 5. Cols 29-36: 볼륨 바 ('#' / '-') */
        {
            int filled = (int)(p->tracks[t].volume * 8 + 0.5f);
            if (filled > 8) filled = 8;
            for (int i = 0; i < 8; i++) {
                row[C_VOL + i] = (i < filled) ? '#' : '-';
            }
        }

        /* 6. Cols 37-42: 볼륨 퍼센트 (%3d%% 문자열 유실 및 오버런 차단) */
        {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "%3d%%", (int)(p->tracks[t].volume * 100 + 0.5f));
            int tl = (int)strlen(tmp);
            for (int i = 0; i < 6; i++) {
                row[C_VPCT + i] = (i < tl) ? tmp[i] : ' ';
            }
        }

        /* 7. Cols 43-49: 노트 카운트 (%-7d 문자열 안전 대입) */
        {
            char tmp[12];
            snprintf(tmp, sizeof(tmp), "%-7d", p->tracks[t].event_count);
            int tl = (int)strlen(tmp);
            for (int i = 0; i < 7; i++) {
                row[C_NOTE + i] = (i < tl) ? tmp[i] : ' ';
            }
        }

        /* 8. Cols 50-56: 베이스 노트 (7자 고정폭) */
        {
            int bn = p->tracks[t].base_note;
            const char *bns = (bn < 0) ? "PERC" : midi_note_name(bn);
            int bl = (int)strlen(bns);
            for (int i = 0; i < 7; i++) {
                row[C_BASE + i] = (i < bl) ? bns[i] : ' ';
            }
        }

        /* 9. Cols 57-91: 악기 이름 (최대 35자 잘림 경고 원천 차단) */
        {
            const char *ip = p->tracks[t].instrument;
            const char *sl = strrchr(ip, '/');
            const char *inm = (sl && sl[1]) ? sl + 1 : ip;
            if (!inm[0]) inm = "(none)";
            int il = (int)strlen(inm);
            for (int i = 0; i < 35; i++) {
                row[C_INST + i] = (i < il) ? inm[i] : ' ';
            }
        }

        /* 10. Col 92+: [EDIT] 마커 처리 */
        if (t == g_editor.cur_track && g_editor.mode == MODE_EDIT) {
            memcpy(row + C_EDIT, "[EDIT]", 6);
        }

        /* 단일 한 줄 문자열 상태로 출력 (ANSI 시퀀스 밀림 방지) */
        attrset(A_NORMAL);
        mvaddnstr(r, 0, row, COLS < TERM_COLS ? COLS : TERM_COLS);

        /* 후처리 컬러 그래픽 마스킹 적용 */
        short sel_pair = is_sel ? 3 : 6;
        attr_t sel_attr = is_sel ? A_BOLD : A_NORMAL;

        mvchgat(r, C_SEL, C_MUTE - C_SEL,  sel_attr,  sel_pair, NULL);
        if (p->tracks[t].mute)
            mvchgat(r, C_MUTE, 6, A_BOLD, 4, NULL);
        mvchgat(r, C_VOL,  8, A_NORMAL, 2, NULL);
        mvchgat(r, C_VPCT, 6, sel_attr,  sel_pair, NULL);
        mvchgat(r, C_NOTE, 7, sel_attr,  sel_pair, NULL);
        mvchgat(r, C_BASE, 7, sel_attr,  sel_pair, NULL);
        mvchgat(r, C_INST, 35, A_NORMAL, 5, NULL);
        if (t == g_editor.cur_track && g_editor.mode == MODE_EDIT)
            mvchgat(r, C_EDIT, 6, A_BOLD, 9, NULL);
    }

    

    #undef C_SEL
    #undef C_NAME
    #undef C_MUTE
    #undef C_VOL
    #undef C_VPCT
    #undef C_NOTE
    #undef C_BASE
    #undef C_INST
    #undef C_EDIT

    /* Progress bar */
    int pb_row = LINES - HELP_ROWS - 1;
    mvhline(pb_row - 1, 0, ACS_HLINE, COLS);
    draw_progress_bar(p, pb_row);
}

/* ── note shape helper ── */
static void note_shape_at(const Project *p, int track,
                           uint32_t tick, int note, char out[3])
{
    int cur = editor_note_at(p, track, tick, note);
    if (!cur) { out[0]=out[1]=out[2]=0; return; }
    int same_prev = 0, same_next = 0;
    const Track *t = &p->tracks[track];
    for (int i = 0; i < t->event_count; i++) {
        const NoteEvent *ev = &t->events[i];
        if (ev->note != (uint8_t)note) continue;
        if (tick >= ev->start_tick && tick < ev->start_tick + ev->duration_tick) {
            if (tick > ev->start_tick)                          same_prev = 1;
            if (tick + 1 < ev->start_tick + ev->duration_tick) same_next = 1;
            break;
        }
    }
    out[0] = same_prev ? '#' : '[';
    out[1] = '#';
    out[2] = same_next ? '#' : ']';
}

/* ═══════════════════════════════════════════════
 *  PIANOROLL DRAW
 * ═══════════════════════════════════════════════ */

void editor_draw_pianoroll(const Project *p)
{
    int row_start  = HEADER_ROWS;
    int col_start  = NOTE_LABEL_COLS;
    int avail_rows = LINES - HEADER_ROWS - HELP_ROWS - 2; /* -2 for info+prog */
    int vcols      = visible_tick_cols();

    const Track *ct = &p->tracks[g_editor.cur_track];

    /* Note range depends on base_note; if percussion (base_note<0), use full C2-B3 */
    int note_min, note_max;
    if (ct->base_note < 0) {
        note_min = 36; note_max = 59; /* C2-B3 for percussion */
    } else {
        note_min = TRACK_NOTE_MIN(ct);
        note_max = TRACK_NOTE_MAX(ct);
    }
    if (note_min < 0)   note_min = 0;
    if (note_max > 127) note_max = 127;

    /* Clamp cursor */
    if (g_editor.cur_note < note_min) g_editor.cur_note = note_min;
    if (g_editor.cur_note > note_max) g_editor.cur_note = note_max;

    /* Clamp horizontal view:
     * - When playing: follow the playhead (keep it ~25% from left edge)
     * - When stopped: follow the edit cursor with right-side margin */
    #define HSCROLL_RIGHT_MARGIN 6
    if (audio_is_playing()) {
        uint32_t ph = audio_current_tick();
        if (ph < (uint32_t)g_editor.view_tick_start ||
            ph >= (uint32_t)(g_editor.view_tick_start + vcols)) {
            int margin = vcols / 4;
            g_editor.view_tick_start = (int)ph - margin;
            if (g_editor.view_tick_start < 0) g_editor.view_tick_start = 0;
        }
    } else {
        if (g_editor.cur_tick < g_editor.view_tick_start)
            g_editor.view_tick_start = g_editor.cur_tick;
        if (g_editor.cur_tick >= g_editor.view_tick_start + vcols - HSCROLL_RIGHT_MARGIN) {
            g_editor.view_tick_start = g_editor.cur_tick - vcols + HSCROLL_RIGHT_MARGIN + 1;
            if (g_editor.view_tick_start < 0) g_editor.view_tick_start = 0;
        }
    }
    g_editor.view_tick_cols = vcols;

    /* Compute playhead column */
    int playhead_col = -1;
    if (audio_is_playing() || audio_is_paused()) {
        uint32_t ph = audio_current_tick();
        if (ph >= (uint32_t)g_editor.view_tick_start &&
            ph <  (uint32_t)(g_editor.view_tick_start + vcols))
            playhead_col = col_start + (int)(ph - g_editor.view_tick_start) * TICK_COL_WIDTH;
    }

    /* Tick ruler */
    for (int tc = 0; tc < vcols; tc++) {
        int tick = g_editor.view_tick_start + tc;
        int col  = col_start + tc * TICK_COL_WIDTH;
        int is_ph = (playhead_col >= 0 && col == playhead_col);
        if (is_ph) {
            attron(COLOR_PAIR(11) | A_BOLD);
            mvprintw(row_start, col, "v  ");
            attrset(A_NORMAL);
        } else if (tick % TICKS_PER_QN == 0) {
            attron(COLOR_PAIR(1));
            mvprintw(row_start, col, "%-3d", tick / TICKS_PER_QN + 1);
            attrset(A_NORMAL);
        } else {
            mvprintw(row_start, col, ".  ");
        }
    }

    /* Piano rows — vertical scroll follows cursor with margin */
    int piano_rows   = note_max - note_min + 1;
    int display_rows = (piano_rows < avail_rows) ? piano_rows : avail_rows;

    #define VSCROLL_MARGIN 3
    /* Clamp view_note_top to valid range */
    if (g_editor.view_note_top > note_max) g_editor.view_note_top = note_max;
    if (g_editor.view_note_top < note_min + display_rows - 1)
        g_editor.view_note_top = note_min + display_rows - 1;
    /* Scroll up when cursor is above visible area */
    if (g_editor.cur_note > g_editor.view_note_top - VSCROLL_MARGIN)
        g_editor.view_note_top = g_editor.cur_note + VSCROLL_MARGIN;
    /* Scroll down when cursor is below visible area */
    if (g_editor.cur_note < g_editor.view_note_top - display_rows + 1 + VSCROLL_MARGIN)
        g_editor.view_note_top = g_editor.cur_note + display_rows - 1 - VSCROLL_MARGIN;
    /* Final clamp */
    if (g_editor.view_note_top > note_max) g_editor.view_note_top = note_max;
    if (g_editor.view_note_top < note_min + display_rows - 1)
        g_editor.view_note_top = note_min + display_rows - 1;

    for (int ri = 0; ri < display_rows; ri++) {
        int note = g_editor.view_note_top - ri;
        if (note < note_min || note > note_max) continue;
        int row  = row_start + 1 + ri;
        if (row >= LINES - HELP_ROWS - 2) break;

        int chroma   = note % 12;
        int is_sharp = (chroma==1||chroma==3||chroma==6||chroma==8||chroma==10);
        int is_cur_row = (note == g_editor.cur_note);

        /* Label column at col 0: highlight selected row, dim sharps */
        if (is_cur_row) {
            attron(COLOR_PAIR(3) | A_BOLD | A_REVERSE);
            mvprintw(row, 0, "%-4s", midi_note_name(note));
            attrset(A_NORMAL);
        } else if (is_sharp) {
            attron(A_DIM);
            mvprintw(row, 0, "%4s", midi_note_name(note));
            attrset(A_NORMAL);
        } else {
            mvprintw(row, 0, "%4s", midi_note_name(note));
            attrset(A_NORMAL);
        }

        /* Tick cells */
        for (int tc = 0; tc < vcols; tc++) {
            int tick     = g_editor.view_tick_start + tc;
            int col      = col_start + tc * TICK_COL_WIDTH;
            int has_note = editor_note_at(p, g_editor.cur_track, tick, note);
            int is_cur   = (tick == g_editor.cur_tick && note == g_editor.cur_note);
            int is_ph    = (playhead_col >= 0 && col == playhead_col);

            int in_span = 0;
            if (g_editor.span_active && note == g_editor.span_note) {
                int s = g_editor.span_start_tick, e = g_editor.cur_tick;
                if (s > e) { int tmp=s; s=e; e=tmp; }
                in_span = (tick >= s && tick <= e);
            }

            if      (is_cur && has_note) attron(COLOR_PAIR(9) | A_BOLD);
            else if (is_cur)             attron(COLOR_PAIR(3) | A_BOLD);
            else if (in_span)            attron(COLOR_PAIR(10));
            else if (has_note)           attron(COLOR_PAIR(2));
            else if (tick % TICKS_PER_QN == 0) attron(A_DIM);

            if (has_note) {
                char sh[3]; note_shape_at(p, g_editor.cur_track, (uint32_t)tick, note, sh);
                mvaddch(row, col, sh[0]);
                mvaddch(row, col+1, sh[1]);
                mvaddch(row, col+2, sh[2]);
            } else if (is_cur) {
                mvaddstr(row, col, "[ ]");
            } else if (in_span) {
                mvaddstr(row, col, "[~]");
            } else if (tick % TICKS_PER_QN == 0) {
                mvaddstr(row, col, "|  ");
            } else {
                mvaddstr(row, col, ".  ");
            }

            attrset(A_NORMAL);

            /* Playhead overlay on empty cells */
            if (is_ph && !has_note && !is_cur) {
                attron(COLOR_PAIR(11) | A_BOLD);
                mvaddch(row, col, '|');
                attrset(A_NORMAL);
            }
        }
    }

    /* Info bar */
    int info_row = LINES - HELP_ROWS - 2;
    if (info_row > HEADER_ROWS) {
        attron(A_REVERSE);
        mvhline(info_row, 0, ' ', COLS);
        attroff(A_REVERSE);
        const char *note_name = midi_note_name(g_editor.cur_note);
        /* Highlighted note name badge */
        attron(COLOR_PAIR(9) | A_BOLD);
        mvprintw(info_row, 1, " %-4s ", note_name);
        attroff(COLOR_PAIR(9) | A_BOLD);
        attron(A_REVERSE);
        mvprintw(info_row, 7,
            " MIDI%3d | Tick:%-4d Bar:%-3d Bt:%d | Trk:%d %-10.10s | BPM:%-3d",
            g_editor.cur_note,
            g_editor.cur_tick,
            g_editor.cur_tick / TICKS_PER_QN + 1,
            g_editor.cur_tick % TICKS_PER_QN + 1,
            g_editor.cur_track + 1,
            p->tracks[g_editor.cur_track].name,
            p->bpm);
        attroff(A_REVERSE);
    }

    /* Progress bar */
    draw_progress_bar(p, LINES - HELP_ROWS - 1);
}

/* ── FILE mode ── */
void editor_draw_file(const Project *p)
{
    int pw = 64, px = (COLS - pw) / 2;
    if (px < 1) px = 1;
    int r = HEADER_ROWS + 1;

    /* Title bar */
    attron(COLOR_PAIR(8) | A_BOLD);
    mvhline(r, px, ' ', pw);
    mvprintw(r, px + (pw-14)/2, "  FILE MANAGER  ");
    attroff(COLOR_PAIR(8) | A_BOLD);
    r++;

    mvaddch(r, px, ACS_ULCORNER); mvhline(r, px+1, ACS_HLINE, pw-2);
    mvaddch(r, px+pw-1, ACS_URCORNER); r++;

    /* Info rows */
    #define FROW(label, fmt, ...) do {          \
        mvaddch(r, px, ACS_VLINE);              \
        attron(A_BOLD);                         \
        mvprintw(r, px+2, " %-8s", label);     \
        attroff(A_BOLD);                        \
        mvprintw(r, px+11, fmt, ##__VA_ARGS__); \
        for(int _i=px+11+20;_i<px+pw-1;_i++) mvaddch(r, _i, ' '); \
        mvaddch(r, px+pw-1, ACS_VLINE); r++;   \
    } while(0)

    FROW("Title",  "%.48s", p->title);
    FROW("Path",   "%.48s", g_editor.file_path);
    int total_ev = 0;
    for (int i = 0; i < p->track_count; i++) total_ev += p->tracks[i].event_count;
    FROW("Stats",  "%d tracks  %d notes  %d BPM", p->track_count, total_ev, p->bpm);
    #undef FROW

    /* Divider */
    mvaddch(r, px, ACS_LTEE); mvhline(r, px+1, ACS_HLINE, pw-2);
    mvaddch(r, px+pw-1, ACS_RTEE); r++;

    /* Stream connection info */
    mvaddch(r, px, ACS_VLINE);
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(r, px+2, " %-*s", pw-5, "STREAM CONNECTION");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvaddch(r, px+pw-1, ACS_VLINE); r++;

    if (g_editor.stream_port > 0) {
        /* Port row */
        mvaddch(r, px, ACS_VLINE);
        attron(A_BOLD); mvprintw(r, px+2, " %-8s", "Port"); attroff(A_BOLD);
        mvprintw(r, px+11, "%d", g_editor.stream_port);
        mvprintw(r, px+16, "   %d client%s connected",
                 g_editor.stream_clients,
                 g_editor.stream_clients == 1 ? "" : "s");
        for (int _i = px+44; _i < px+pw-1; _i++) mvaddch(r, _i, ' ');
        mvaddch(r, px+pw-1, ACS_VLINE); r++;

        /* Enumerate non-loopback IPv4 addresses */
        struct ifaddrs *ifap = NULL;
        getifaddrs(&ifap);

        int ip_count = 0;
        for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
            /* skip loopback (127.x.x.x) */
            if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127) continue;

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));

            mvaddch(r, px, ACS_VLINE);
            if (ip_count == 0) {
                attron(A_BOLD); mvprintw(r, px+2, " %-8s", "Address"); attroff(A_BOLD);
            } else {
                mvprintw(r, px+2, "          "); /* indent continuation */
            }
            attron(A_BOLD | COLOR_PAIR(2));
            mvprintw(r, px+11, "%-16s", ip);
            attroff(A_BOLD | COLOR_PAIR(2));
            mvprintw(r, px+28, "(%s)", ifa->ifa_name);
            for (int _i = px+28+2+(int)strlen(ifa->ifa_name)+1; _i < px+pw-1; _i++)
                mvaddch(r, _i, ' ');
            mvaddch(r, px+pw-1, ACS_VLINE); r++;
            ip_count++;
        }
        if (ifap) freeifaddrs(ifap);

        if (ip_count == 0) {
            /* No non-loopback interface found */
            mvaddch(r, px, ACS_VLINE);
            attron(A_BOLD); mvprintw(r, px+2, " %-8s", "Address"); attroff(A_BOLD);
            mvprintw(r, px+11, "%-*s", pw-13, "(no external interface found)");
            mvaddch(r, px+pw-1, ACS_VLINE); r++;
        }

        /* Hint row */
        mvaddch(r, px, ACS_VLINE);
        attron(A_DIM);
        mvprintw(r, px+2, " Connect from bbeat_client: E → enter IP:port above");
        for (int _i = px+54; _i < px+pw-1; _i++) mvaddch(r, _i, ' ');
        attroff(A_DIM);
        mvaddch(r, px+pw-1, ACS_VLINE); r++;

    } else {
        /* Streaming not active */
        mvaddch(r, px, ACS_VLINE);
        attron(A_DIM);
        mvprintw(r, px+2, " %-*s", pw-5,
                 "Not active  (use --audio=stream or --audio=both)");
        attroff(A_DIM);
        mvaddch(r, px+pw-1, ACS_VLINE); r++;
    }

    /* Divider before actions */
    mvaddch(r, px, ACS_LTEE); mvhline(r, px+1, ACS_HLINE, pw-2);
    mvaddch(r, px+pw-1, ACS_RTEE); r++;

    /* Actions */
    mvaddch(r, px, ACS_VLINE);
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(r, px+2, " %-*s", pw-5, "ACTIONS");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvaddch(r, px+pw-1, ACS_VLINE); r++;

    #define AROW(key, label, desc) do {                     \
        mvaddch(r, px, ACS_VLINE);                          \
        attron(COLOR_PAIR(3) | A_BOLD);                     \
        mvprintw(r, px+3, " %-5s", key);                   \
        attroff(COLOR_PAIR(3) | A_BOLD);                    \
        attron(A_BOLD);                                     \
        mvprintw(r, px+9, "%-14s", label);                  \
        attroff(A_BOLD);                                    \
        mvprintw(r, px+23, "%-40.40s", desc);              \
        mvaddch(r, px+pw-1, ACS_VLINE); r++;                \
    } while(0)

    AROW("S",   "Save",         "Save project to current path");
    AROW("L",   "Load",         "Browse saves/ and open a project");
    AROW("N",   "New",          "Create new empty project");
    AROW("R",   "Rename path",  "Change save/load file path manually");
    AROW("T",   "Set title",    "Rename this project");
    AROW("E",   "Export WAV",   "Render project beside current save path");
    AROW("ESC", "Back",         "Return to previous mode");
    #undef AROW

    mvaddch(r, px, ACS_LLCORNER); mvhline(r, px+1, ACS_HLINE, pw-2);
    mvaddch(r, px+pw-1, ACS_LRCORNER);
}


/* ── Master draw ── */
static void draw_all(const Project *p)
{
    erase();
    editor_draw_header(p);

    switch (g_editor.mode) {
    case MODE_TRACK: editor_draw_track(p);     break;
    case MODE_EDIT:  editor_draw_pianoroll(p); break;
    case MODE_FILE:  editor_draw_file(p);      break;
    default:         break;
    }

    editor_draw_help();
    refresh();
}

/* ═══════════════════════════════════════════════
 *  NOTE OPERATIONS
 * ═══════════════════════════════════════════════ */

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
                 "Track full (MAX_EVENTS=%d).", MAX_EVENTS);
        return;
    }
    NoteEvent *ev   = &t->events[t->event_count++];
    ev->start_tick    = start;
    ev->duration_tick = dur > 0 ? dur : 1;
    ev->note          = (uint8_t)note;
    ev->velocity      = vel;
    pthread_mutex_unlock(&g_project_mtx);
    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "Note placed: %s tick=%u dur=%u", midi_note_name(note), start, dur);
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
                     "Note deleted: %s tick=%u", midi_note_name(note), tick);
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
    g_editor.span_active = 0;
    g_editor.mode = m;

    switch (m) {
    case MODE_TRACK:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "TRACK: Enter edits the selected track. ESC returns to intro.");
        break;
    case MODE_EDIT:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "EDIT: Move with arrows. During playback, moving pauses and retimes playback.");
        break;
    case MODE_FILE:
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "FILE: Save/load projects or export the current song as WAV.");
        break;
    default: break;
    }
    (void)p;
}

/* ═══════════════════════════════════════════════
 *  TRACK MODE KEY HANDLER
 * ═══════════════════════════════════════════════ */

static void handle_track_key(int ch, Project *p)
{
    switch (ch) {
    case 27:
        audio_stop();
        g_editor.exit_to_intro = 1;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Returning to intro...");
        break;

    /* Navigation */
    case KEY_UP:
        if (g_editor.track_cursor > 0) g_editor.track_cursor--;
        break;
    case KEY_DOWN:
        if (g_editor.track_cursor < p->track_count - 1) g_editor.track_cursor++;
        break;

    /* Playhead seek */
    case KEY_LEFT:
        if (g_editor.play_cursor >= (uint32_t)TICKS_PER_QN)
            g_editor.play_cursor -= TICKS_PER_QN;
        else
            g_editor.play_cursor = 0;
        audio_seek_tick(g_editor.play_cursor);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Seek -> tick %u", g_editor.play_cursor);
        break;
    case KEY_RIGHT:
        g_editor.play_cursor += TICKS_PER_QN;
        audio_seek_tick(g_editor.play_cursor);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Seek -> tick %u", g_editor.play_cursor);
        break;

    /* 0: jump playhead to start */
    case '0':
        g_editor.play_cursor = 0;
        audio_seek_tick(0);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Seek -> start (tick 0)");
        break;

    /* Rename selected track */
    case 'n': case 'N': {
        char buf[32] = {0};
        prompt_string("Rename track: ", buf, 31);
        if (buf[0]) {
            strncpy(p->tracks[g_editor.track_cursor].name, buf, 31);
            p->tracks[g_editor.track_cursor].name[31] = '\0';
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d renamed to: %s", g_editor.track_cursor + 1, buf);
        }
        break; }

    /* BPM control: , / . */
    case ',': {
        int bpm = audio_get_bpm() - 5;
        if (bpm < 20) bpm = 20;
        audio_set_bpm(bpm); p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "BPM -> %d", bpm);
        break; }
    case '.': {
        int bpm = audio_get_bpm() + 5;
        if (bpm > 300) bpm = 300;
        audio_set_bpm(bpm); p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "BPM -> %d", bpm);
        break; }

    /* Enter: open edit for selected track */
    case '\n': case '\r': case KEY_ENTER:
        g_editor.cur_track  = g_editor.track_cursor;
        g_editor.cur_note   = (p->tracks[g_editor.cur_track].base_note >= 0)
                               ? p->tracks[g_editor.cur_track].base_note : 48;
        /* Move cursor to playhead position when entering edit */
        g_editor.cur_tick   = audio_current_tick();
        g_editor.view_tick_start = ((uint32_t)g_editor.cur_tick > (uint32_t)visible_tick_cols()/2)
                                   ? g_editor.cur_tick - visible_tick_cols()/2 : 0;
        g_editor.view_note_top = (p->tracks[g_editor.cur_track].base_note >= 0)
                                 ? p->tracks[g_editor.cur_track].base_note + 11 : 59;
        editor_set_mode(MODE_EDIT, p);
        break;

    /* Space: play / pause */
    case ' ':
        if (audio_is_playing()) {
            audio_pause();
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Paused.");
        } else if (audio_is_paused()) {
            audio_resume();
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "Resumed.");
        } else {
            /* Start from play_cursor */
            audio_seek_tick(g_editor.play_cursor);
            audio_play(p);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Playing from tick %u.", g_editor.play_cursor);
        }
        break;

    /* Mute */
    case 'm': case 'M':
        p->tracks[g_editor.track_cursor].mute ^= 1;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d %s", g_editor.track_cursor + 1,
                 p->tracks[g_editor.track_cursor].mute ? "MUTED" : "unmuted");
        break;

    /* Volume */
    case '+': case '=': {
        float v = p->tracks[g_editor.track_cursor].volume + 0.05f;
        if (v > 1.0f) v = 1.0f;
        p->tracks[g_editor.track_cursor].volume = v;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d vol: %d%%", g_editor.track_cursor+1, (int)(v*100));
        break; }
    case '-': {
        float v = p->tracks[g_editor.track_cursor].volume - 0.05f;
        if (v < 0.0f) v = 0.0f;
        p->tracks[g_editor.track_cursor].volume = v;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d vol: %d%%", g_editor.track_cursor+1, (int)(v*100));
        break; }

    /* Add track */
    case 'a': case 'A': {
        if (p->track_count >= MAX_TRACKS) {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Max %d tracks reached.", MAX_TRACKS);
            break;
        }
        char instr[128] = {0};
        if (!pick_instrument(instr)) {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track creation cancelled.");
            break;
        }
        int bn = pick_base_note();
        int idx = p->track_count++;
        snprintf(p->tracks[idx].name, 31, "Track%d", idx+1);
        strncpy(p->tracks[idx].instrument, instr, 127);
        p->tracks[idx].instrument[127] = '\0';
        p->tracks[idx].volume      = 1.0f;
        p->tracks[idx].mute        = 0;
        p->tracks[idx].event_count = 0;
        p->tracks[idx].base_note   = bn;
        g_editor.track_cursor = idx;
        audio_load_instrument(idx, instr);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d added. Base: %s", idx+1,
                 bn < 0 ? "PERC" : midi_note_name(bn));
        break; }

    /* Change instrument */
    case 'i': case 'I': {
        char instr[128] = {0};
        if (pick_instrument(instr)) {
            strncpy(p->tracks[g_editor.track_cursor].instrument, instr, 127);
            p->tracks[g_editor.track_cursor].instrument[127] = '\0';
            audio_load_instrument(g_editor.track_cursor, instr);
            int bn = pick_base_note();
            p->tracks[g_editor.track_cursor].base_note = bn;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Instrument changed. Base: %s",
                     bn < 0 ? "PERC" : midi_note_name(bn));
        }
        break; }

    /* Change base note */
    case 'b': case 'B': {
        int bn = pick_base_note();
        p->tracks[g_editor.track_cursor].base_note = bn;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d base note: %s", g_editor.track_cursor+1,
                 bn < 0 ? "PERC" : midi_note_name(bn));
        break; }

    /* Duplicate track — insert copy right after selected, with optional transpose */
    case 'c': case 'C': {
        if (p->track_count >= MAX_TRACKS) {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Max %d tracks reached.", MAX_TRACKS);
            break;
        }
        int src = g_editor.track_cursor;
        int dst = src + 1;
        /* Save source name before any moves */
        char src_name[32];
        memcpy(src_name, p->tracks[src].name, sizeof(src_name));
        /* Shift everything from dst onwards one position down */
        for (int i = p->track_count; i > dst; i--)
            p->tracks[i] = p->tracks[i - 1];
        /* Deep-copy source into dst */
        p->tracks[dst] = p->tracks[src];
        /* Give copy a distinct name (append "cp", max 31 chars) */
        snprintf(p->tracks[dst].name, sizeof(p->tracks[dst].name),
                 "%.27scp", src_name);
        p->track_count++;

        /* Ask for transpose amount — only shift note events, not base_note.
         * base_note defines the sample tuning; shifting both cancels out. */
        int tr = pick_transpose();
        if (tr != 0) {
            for (int e = 0; e < p->tracks[dst].event_count; e++) {
                int n = (int)p->tracks[dst].events[e].note + tr;
                if (n < 0)   n = 0;
                if (n > 127) n = 127;
                p->tracks[dst].events[e].note = (uint8_t)n;
            }
        }

        /* Keep cur_track pointing at the same track if it shifted */
        if (g_editor.cur_track >= dst)
            g_editor.cur_track++;
        g_editor.track_cursor = dst;
        /* Re-register instruments for all tracks at dst and beyond */
        for (int i = dst; i < p->track_count; i++)
            audio_load_instrument(i, p->tracks[i].instrument);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Track %d duplicated as Track %d%s.", src + 1, dst + 1,
                 tr != 0 ? " (transposed)" : "");
        break; }

    /* Delete track */
    case 'd': case 'D':
        if (p->track_count > 1) {
            int idx = g_editor.track_cursor;
            for (int i = idx; i < p->track_count-1; i++)
                p->tracks[i] = p->tracks[i+1];
            p->track_count--;
            if (g_editor.track_cursor >= p->track_count)
                g_editor.track_cursor = p->track_count - 1;
            if (g_editor.cur_track >= p->track_count)
                g_editor.cur_track = p->track_count - 1;
            for (int i = 0; i < p->track_count; i++)
                audio_load_instrument(i, p->tracks[i].instrument);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Track %d deleted.", idx+1);
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Cannot delete last track.");
        }
        break;

    default: break;
    }
}

/* ═══════════════════════════════════════════════
 *  EDIT MODE KEY HANDLER
 * ═══════════════════════════════════════════════ */

/* Jump helpers: find first/last tick that has any note in cur_track */
static uint32_t track_last_tick(const Project *p)
{
    const Track *t = &p->tracks[g_editor.cur_track];
    uint32_t last = 0;
    for (int i = 0; i < t->event_count; i++) {
        uint32_t e = t->events[i].start_tick + t->events[i].duration_tick - 1;
        if (e > last) last = e;
    }
    return last;
}

static void pause_after_edit_seek(void)
{
    if (!audio_is_playing()) return;
    audio_seek_tick((uint32_t)g_editor.cur_tick);
    audio_pause();
    g_editor.play_cursor = (uint32_t)g_editor.cur_tick;
    snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
             "Paused at tick %d. Press Space to play from here.", g_editor.cur_tick);
}

static void handle_edit_key(int ch, Project *p)
{
    const Track *ct = &p->tracks[g_editor.cur_track];
    int note_min = (ct->base_note < 0) ? 36 : TRACK_NOTE_MIN(ct);
    int note_max = (ct->base_note < 0) ? 59 : TRACK_NOTE_MAX(ct);
    if (note_min < 0)   note_min = 0;
    if (note_max > 127) note_max = 127;

    int note = g_editor.cur_note;
    int tick = g_editor.cur_tick;

    switch (ch) {
    /* ESC: back to TRACK */
    case 27:
        g_editor.span_active = 0;
        editor_set_mode(MODE_TRACK, p);
        break;

    /* Arrow: cursor movement */
    case KEY_UP:
        if (g_editor.cur_note < note_max) g_editor.cur_note++;
        pause_after_edit_seek();
        break;
    case KEY_DOWN:
        if (g_editor.cur_note > note_min) g_editor.cur_note--;
        pause_after_edit_seek();
        break;
    case KEY_RIGHT:
        g_editor.cur_tick++;
        pause_after_edit_seek();
        break;
    case KEY_LEFT:
        if (g_editor.cur_tick > 0) g_editor.cur_tick--;
        pause_after_edit_seek();
        break;

    /* Ctrl+Right: jump to last note */
    case KEY_CTRL('f'):   /* some terminals send 06 */
    case 518: case 560:   /* KEY_SRIGHT variants */
        g_editor.cur_tick = track_last_tick(p);
        pause_after_edit_seek();
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Jumped to last note (tick %u).", g_editor.cur_tick);
        break;

    /* 0: jump to start (tick 0) */
    case '0':
        g_editor.cur_tick = 0;
        pause_after_edit_seek();
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Jumped to start (tick 0).");
        break;

    /* Space: play / pause — always start/resume from cursor position */
    case ' ':
        if (audio_is_playing()) {
            audio_pause();
            /* Sync cursor to where we stopped */
            g_editor.cur_tick = (int)audio_current_tick();
            g_editor.play_cursor = (uint32_t)g_editor.cur_tick;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Paused at tick %d. Press Space to restart from cursor.", g_editor.cur_tick);
        } else {
            /* Always restart from current cursor, even if paused */
            audio_seek_tick((uint32_t)g_editor.cur_tick);
            audio_play(p);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Playing from tick %d (%s).",
                     g_editor.cur_tick,
                     midi_note_name(g_editor.cur_note));
        }
        break;

    /* Enter: place / toggle note */
    case '\n': case '\r': case KEY_ENTER:
        if (editor_note_at(p, g_editor.cur_track, tick, note))
            editor_delete_note(p, tick, note);
        else
            editor_place_note(p, tick, 1, note, 80);
        break;

    /* Span start */
    case ',':
        if (!g_editor.span_active) {
            g_editor.span_active     = 1;
            g_editor.span_start_tick = tick;
            g_editor.span_note       = note;
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Span start at tick=%d %s  ->  move, press '.'",
                     tick, midi_note_name(note));
        }
        break;

    /* Span end */
    case '.':
        if (g_editor.span_active) {
            int s = g_editor.span_start_tick, e = tick;
            int sn = g_editor.span_note;
            if (s > e) { int t2=s; s=e; e=t2; }
            editor_place_note(p, (uint32_t)s, (uint32_t)(e-s+1), sn, 80);
            g_editor.span_active = 0;
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "No span active. Press ',' first.");
        }
        break;

    /* Delete */
    case KEY_DC: case 127:
        editor_delete_note(p, tick, note);
        break;

    /* BPM */
    case '+': case '=': {
        int bpm = audio_get_bpm() + 5;
        if (bpm > 300) bpm = 300;
        audio_set_bpm(bpm); p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "BPM -> %d", bpm);
        break; }
    case '-': {
        int bpm = audio_get_bpm() - 5;
        if (bpm < 20) bpm = 20;
        audio_set_bpm(bpm); p->bpm = bpm;
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "BPM -> %d", bpm);
        break; }

    /* [ / ] : switch track in edit mode */
    case '[':
        if (g_editor.cur_track > 0) {
            g_editor.cur_track--;
            g_editor.cur_note = (p->tracks[g_editor.cur_track].base_note >= 0)
                                ? p->tracks[g_editor.cur_track].base_note : 48;
            g_editor.view_note_top = (p->tracks[g_editor.cur_track].base_note >= 0)
                                    ? p->tracks[g_editor.cur_track].base_note + 11 : 59;
        }
        break;
    case ']':
        if (g_editor.cur_track < p->track_count - 1) {
            g_editor.cur_track++;
            g_editor.cur_note = (p->tracks[g_editor.cur_track].base_note >= 0)
                                ? p->tracks[g_editor.cur_track].base_note : 48;
            g_editor.view_note_top = (p->tracks[g_editor.cur_track].base_note >= 0)
                                    ? p->tracks[g_editor.cur_track].base_note + 11 : 59;
        }
        break;

    default: break;
    }
}

/* ═══════════════════════════════════════════════
 *  PICKERS (blocking sub-loops)
 * ═══════════════════════════════════════════════ */

/* Fuzzy filter: returns 1 if 'query' is a substring of 'haystack' (case-insensitive) */
static int fuzzy_match(const char *haystack, const char *query)
{
    if (!query[0]) return 1;
    /* Simple substring search, case-insensitive */
    int ql = (int)strlen(query);
    int hl = (int)strlen(haystack);
    for (int i = 0; i <= hl - ql; i++) {
        int match = 1;
        for (int j = 0; j < ql && match; j++) {
            char a = haystack[i+j], b = query[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) match = 0;
        }
        if (match) return 1;
    }
    return 0;
}

static int pick_instrument(char out[128])
{
    char files[64][264];
    int  nfiles = file_list_instruments(files, 64);
    if (nfiles == 0) {
        snprintf(out, 128, "%s/silent.wav", SAMPLES_DIR);
        return 1;
    }

    nodelay(stdscr, FALSE);
    char query[64] = {0};
    int  sel = 0, done = 0, accepted = 0;

    while (!done) {
        /* Build filtered list */
        int fidx[64]; int nf = 0;
        for (int i = 0; i < nfiles; i++) {
            const char *sl = strrchr(files[i], '/');
            const char *nm = sl ? sl+1 : files[i];
            if (fuzzy_match(nm, query)) fidx[nf++] = i;
        }
        if (sel >= nf) sel = nf > 0 ? nf-1 : 0;

        int r = HEADER_ROWS + 1;
        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r-1, 0, ' ', COLS);
        mvprintw(r-1, 2, "SELECT INSTRUMENT  Up/Dn:move  Enter:select  Esc:cancel  Type to filter");
        attrset(A_NORMAL);
        attron(COLOR_PAIR(3));
        mvprintw(r, 2, "Filter: [%-20s]", query);
        attrset(A_NORMAL);
        r++;

        for (int i = 0; i < nf && i < 20; i++) {
            const char *sl = strrchr(files[fidx[i]], '/');
            const char *nm = sl ? sl+1 : files[fidx[i]];
            if (i == sel) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(r+i, 4, "> %-40s", nm); attrset(A_NORMAL); }
            else           { mvprintw(r+i, 4, "  %-40s", nm); }
        }
        if (nf == 0) mvprintw(r, 4, "  (no matches)");
        refresh();

        int ch = getch();
        switch (ch) {
        case KEY_UP:    if (sel > 0) sel--;           break;
        case KEY_DOWN:  if (sel < nf-1) sel++;        break;
        case '\n': case '\r': case KEY_ENTER:
            if (nf > 0) {
                strncpy(out, files[fidx[sel]], 127);
                out[127] = '\0';
                accepted=1;
            }
            done = 1; break;
        case 27:        done = 1; break;
        case KEY_BACKSPACE: case 127: {
            int ql = (int)strlen(query);
            if (ql > 0) { query[ql-1]='\0'; sel=0; }
            break; }
        default:
            if (ch >= 32 && ch < 127 && (int)strlen(query) < 63) {
                query[strlen(query)] = (char)ch; sel = 0;
            }
            break;
        }
    }
    nodelay(stdscr, TRUE);
    return accepted;
}

/* pick_savefile: browse saves/ directory with search */
static int pick_savefile(char out[256])
{
    /* Collect .bbeat files in SAVES_DIR */
    char files[128][512];
    int  nfiles = 0;
    DIR *dp = opendir(SAVES_DIR);
    if (dp) {
        struct dirent *ent;
        while ((ent = readdir(dp)) && nfiles < 128) {
            size_t len = strlen(ent->d_name);
            if (len > 6 && strcmp(ent->d_name + len - 6, ".bbeat") == 0) {
                snprintf(files[nfiles], sizeof(files[nfiles]), "%s/%s", SAVES_DIR, ent->d_name);
                nfiles++;
            }
        }
        closedir(dp);
    }
    if (nfiles == 0) {
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "No .bbeat files found in saves/.");
        return 0;
    }

    nodelay(stdscr, FALSE);
    char query[64] = {0};
    int  sel = 0, done = 0, accepted = 0;

    while (!done) {
        int fidx[128]; int nf = 0;
        for (int i = 0; i < nfiles; i++) {
            const char *sl = strrchr(files[i], '/');
            const char *nm = sl ? sl+1 : files[i];
            if (fuzzy_match(nm, query)) fidx[nf++] = i;
        }
        if (sel >= nf) sel = nf > 0 ? nf-1 : 0;

        int r = HEADER_ROWS + 1;
        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r-1, 0, ' ', COLS);
        mvprintw(r-1, 2, "LOAD PROJECT  Up/Dn:move  Enter:open  Esc:cancel  Type to filter");
        attrset(A_NORMAL);
        attron(COLOR_PAIR(3));
        mvprintw(r, 2, "Filter: [%-20s]", query);
        attrset(A_NORMAL);
        r++;

        for (int i = 0; i < nf && i < 20; i++) {
            const char *sl = strrchr(files[fidx[i]], '/');
            const char *nm = sl ? sl+1 : files[fidx[i]];
            if (i == sel) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(r+i, 4, "> %-50s", nm); attrset(A_NORMAL); }
            else           { mvprintw(r+i, 4, "  %-50s", nm); }
        }
        if (nf == 0) mvprintw(r, 4, "  (no .bbeat files match)");
        refresh();

        int ch = getch();
        switch (ch) {
        case KEY_UP:   if (sel > 0) sel--;      break;
        case KEY_DOWN: if (sel < nf-1) sel++;   break;
        case '\n': case '\r': case KEY_ENTER:
            if (nf > 0) {
                strncpy(out, files[fidx[sel]], 255);
                out[255] = '\0';
                accepted=1;
            }
            done = 1; break;
        case 27: done = 1; break;
        case KEY_BACKSPACE: case 127: {
            int ql = (int)strlen(query);
            if (ql > 0) { query[ql-1]='\0'; sel=0; }
            break; }
        default:
            if (ch >= 32 && ch < 127 && (int)strlen(query) < 63) {
                query[strlen(query)] = (char)ch; sel = 0;
            }
            break;
        }
    }
    nodelay(stdscr, TRUE);
    return accepted;
}

/* pick_base_note: select octave then note, or percussion */
static int pick_base_note(void)
{
    static const char *NOTE_NAMES[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    nodelay(stdscr, FALSE);

    /* Step 1: select type */
    int type_sel = 0; /* 0=pitched, 1=percussion */
    int done = 0;
    while (!done) {
        int r = HEADER_ROWS + 2;
        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r-2, 0, ' ', COLS);
        mvprintw(r-2, 2, "BASE NOTE: Select instrument type");
        attrset(A_NORMAL);
        if (type_sel == 0) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(r,   4, "> Pitched (select note + octave)"); attrset(A_NORMAL); }
        else                 mvprintw(r,   4, "  Pitched (select note + octave)");
        if (type_sel == 1) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(r+1, 4, "> Percussion / Unknown (no pitch)"); attrset(A_NORMAL); }
        else                 mvprintw(r+1, 4, "  Percussion / Unknown (no pitch)");
        refresh();

        int ch = getch();
        if (ch == KEY_UP && type_sel > 0) type_sel--;
        else if (ch == KEY_DOWN && type_sel < 1) type_sel++;
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) done = 1;
        else if (ch == 27) { nodelay(stdscr, TRUE); return 60; } /* default */
    }
    if (type_sel == 1) { nodelay(stdscr, TRUE); return -1; } /* percussion */

    /* Step 2: select octave (0-8) */
    int oct_sel = 4; done = 0;
    while (!done) {
        int r = HEADER_ROWS + 2;
        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r-2, 0, ' ', COLS);
        mvprintw(r-2, 2, "BASE NOTE: Select octave  Left/Right or type digit");
        attrset(A_NORMAL);
        for (int o = 0; o <= 8; o++) {
            if (o == oct_sel) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(r, 4 + o*5, "[%d]  ", o); attrset(A_NORMAL); }
            else                mvprintw(r, 4 + o*5, " %d   ", o);
        }
        mvprintw(r+1, 4, "Currently: C%d (MIDI %d)", oct_sel, (oct_sel+1)*12);
        refresh();

        int ch = getch();
        if (ch == KEY_LEFT && oct_sel > 0) oct_sel--;
        else if (ch == KEY_RIGHT && oct_sel < 8) oct_sel++;
        else if (ch >= '0' && ch <= '8') oct_sel = ch - '0';
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) done = 1;
        else if (ch == 27) { nodelay(stdscr, TRUE); return 60; }
    }

    /* Step 3: select note within octave */
    int note_sel = 0; done = 0; /* 0=C, 1=C#, ... 11=B */
    while (!done) {
        int r = HEADER_ROWS + 2;
        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r-2, 0, ' ', COLS);
        mvprintw(r-2, 2, "BASE NOTE: Select note  Left/Right:move  Enter:confirm");
        attrset(A_NORMAL);
        for (int n = 0; n < 12; n++) {
            int is_sharp = (n==1||n==3||n==6||n==8||n==10);
            if (n == note_sel) { attron(COLOR_PAIR(3)|A_BOLD); }
            else if (is_sharp)  { attron(A_DIM); }
            mvprintw(r, 4 + n*5, " %-4s", NOTE_NAMES[n]);
            attrset(A_NORMAL);
        }
        int midi = (oct_sel + 1) * 12 + note_sel;
        mvprintw(r+1, 4, "Selected: %s%d  (MIDI %d)", NOTE_NAMES[note_sel], oct_sel, midi);
        refresh();

        int ch = getch();
        if (ch == KEY_LEFT && note_sel > 0) note_sel--;
        else if (ch == KEY_RIGHT && note_sel < 11) note_sel++;
        else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) done = 1;
        else if (ch == 27) { nodelay(stdscr, TRUE); return 60; }
    }

    nodelay(stdscr, TRUE);
    return (oct_sel + 1) * 12 + note_sel;
}

/* ── Semitone / interval name ── */
static const char *semitone_name(int st)
{
    switch (st) {
    case -24: return "2 oct down";
    case -12: return "1 oct down";
    case  -7: return "P5 down";
    case  -5: return "P4 down";
    case  -4: return "M3 down";
    case  -3: return "m3 down";
    case  -2: return "M2 down";
    case  -1: return "m2 down";
    case   0: return "Unison (no transpose)";
    case   1: return "m2";
    case   2: return "M2";
    case   3: return "m3  (minor chord)";
    case   4: return "M3  (major chord)";
    case   5: return "P4";
    case   7: return "P5";
    case   8: return "m6";
    case   9: return "M6";
    case  10: return "m7";
    case  11: return "M7";
    case  12: return "1 oct up";
    case  24: return "2 oct up";
    default:  return "";
    }
}

/* pick_transpose: choose a semitone offset (-24..+24) for duplication */
static int pick_transpose(void)
{
    #define TR_HALF 24   /* ±24 semitones = ±2 octaves */

    nodelay(stdscr, FALSE);
    int st = 0, done = 0;

    while (!done) {
        int r = HEADER_ROWS + 2;

        attron(COLOR_PAIR(8) | A_BOLD);
        mvhline(r-2, 0, ' ', COLS);
        mvprintw(r-2, 2,
            "TRANSPOSE  Left/Right:±1 semitone   Up/Down:±12 (octave)"
            "   Enter:apply   ESC:no transpose");
        attrset(A_NORMAL);

        /* Clear work area */
        for (int i = -1; i <= 3; i++) mvhline(r+i, 0, ' ', COLS);

        /* Value + interval name */
        attron(COLOR_PAIR(3) | A_BOLD);
        mvprintw(r, 4, "[ %+3d ]   %-26s", st, semitone_name(st));
        attrset(A_NORMAL);

        /* Visual bar: -24 ... 0 ... +24 (49 chars) */
        int bx = 4;   /* left edge of the label "-2oct" */
        mvprintw(r+2, bx, "-2oct");
        bx += 5;
        for (int i = -TR_HALF; i <= TR_HALF; i++) {
            int col = bx + (i + TR_HALF);
            char c;
            if      (i == st)       c = '*';
            else if (i == 0)        c = '|';
            else if (i % 12 == 0)   c = ':';
            else                    c = '-';

            if (i == st)            attron(COLOR_PAIR(3) | A_BOLD);
            else if (i == 0)        attron(A_BOLD);
            mvaddch(r+2, col, (chtype)c);
            attrset(A_NORMAL);
        }
        mvprintw(r+2, bx + TR_HALF*2 + 1, "+2oct");

        /* Octave labels under the bar */
        mvprintw(r+3, bx + TR_HALF - 1, "C  ");
        attron(A_DIM);
        mvprintw(r+3, bx,             "-1oct");
        mvprintw(r+3, bx + TR_HALF*2 - 4, "+1oct");
        attrset(A_NORMAL);

        refresh();

        int ch = getch();
        if      (ch == KEY_LEFT  && st > -TR_HALF) st--;
        else if (ch == KEY_RIGHT && st <  TR_HALF) st++;
        else if (ch == KEY_UP) {
            st += 12;
            if (st > TR_HALF) st = TR_HALF;
        } else if (ch == KEY_DOWN) {
            st -= 12;
            if (st < -TR_HALF) st = -TR_HALF;
        } else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) done = 1;
        else if (ch == 27) { st = 0; done = 1; }
    }

    nodelay(stdscr, TRUE);
    #undef TR_HALF
    return st;
}

/* ── Inline string prompt ── */
static void prompt_string(const char *prompt, char *out, int maxlen)
{
    int r = LINES - HELP_ROWS - 1;
    nodelay(stdscr, FALSE);
    echo();
    attron(COLOR_PAIR(3));
    mvprintw(r, 0, "%-*s", COLS-1, "");
    mvprintw(r, 0, "%s", prompt);
    attroff(COLOR_PAIR(3));
    refresh();
    getnstr(out, maxlen - 1);
    noecho();
    nodelay(stdscr, TRUE);
}

/* ═══════════════════════════════════════════════
 *  FILE MODE KEY HANDLER
 * ═══════════════════════════════════════════════ */

static void handle_file_key(int ch, Project **pp)
{
    switch (ch) {
    case 27:
        editor_set_mode(MODE_TRACK, *pp);
        break;

    case 's': case 'S': case KEY_CTRL_S: {
        int ret = project_save(*pp, g_editor.file_path);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 ret == 0 ? "Saved: %s" : "Save FAILED: %s", g_editor.file_path);
        break; }

    case 'l': case 'L': {
        char path[256] = {0};
        if (!pick_savefile(path)) break;
        Project *loaded = project_load(path);
        if (loaded) {
            audio_stop();
            project_free(*pp); *pp = loaded;
            strncpy(g_editor.file_path, path, 255);
            g_editor.file_path[255] = '\0';
            g_editor.cur_track = 0; g_editor.cur_tick = 0;
            g_editor.track_cursor = 0;
            audio_set_bpm((*pp)->bpm);
            for (int i = 0; i < (*pp)->track_count; i++)
                audio_load_instrument(i, (*pp)->tracks[i].instrument);
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Loaded: %.100s", path);
        } else {
            snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                     "Load FAILED: %.100s", path);
        }
        break; }

    case 'n': case 'N': {
        audio_stop();
        project_free(*pp); *pp = project_new(NULL);
        g_editor.cur_track = 0; g_editor.cur_tick = 0; g_editor.track_cursor = 0;
        file_default_path((*pp)->title, g_editor.file_path, sizeof(g_editor.file_path));
        audio_set_bpm((*pp)->bpm);
        audio_load_instrument(0, (*pp)->tracks[0].instrument);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg), "New project.");
        break; }

    case 'r': case 'R': {
        char buf[256] = {0};
        prompt_string("Path: ", buf, 255);
        if (buf[0]) {
            strncpy(g_editor.file_path, buf, 255);
            g_editor.file_path[255] = '\0';
        }
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Path: %.100s", g_editor.file_path);
        break; }

    case 't': case 'T': {
        char buf[64] = {0};
        prompt_string("Title: ", buf, 63);
        if (buf[0]) {
            strncpy((*pp)->title, buf, 63);
            (*pp)->title[63] = '\0';
            file_default_path((*pp)->title, g_editor.file_path, sizeof(g_editor.file_path));
        }
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 "Title: %s", (*pp)->title);
        break; }

    case 'e': case 'E': {
        char wav_path[256];
        strncpy(wav_path, g_editor.file_path, sizeof(wav_path) - 1);
        wav_path[sizeof(wav_path) - 1] = '\0';
        char *dot = strrchr(wav_path, '.');
        if (dot) {
            snprintf(dot, sizeof(wav_path) - (size_t)(dot - wav_path), ".wav");
        } else {
            strncat(wav_path, ".wav", sizeof(wav_path) - strlen(wav_path) - 1);
        }
        int ret = audio_export_wav(*pp, wav_path);
        snprintf(g_editor.status_msg, sizeof(g_editor.status_msg),
                 ret == 0 ? "Exported WAV: %s" : "Export FAILED: %s", wav_path);
        break; }

    default: break;
    }
}

/* Declared in main.c */
extern volatile sig_atomic_t g_sigint_received;

/* ── Quit confirmation overlay ── */
static int confirm_quit(void)
{
    /* Draw a small centered box asking y/n */
    int r = LINES/2 - 2;
    int c = COLS/2 - 18;
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(r,   c, "+------------------------------------+");
    mvprintw(r+1, c, "|  Quit BashBeats?  (y)es  (n)o      |");
    mvprintw(r+2, c, "+------------------------------------+");
    attroff(COLOR_PAIR(4) | A_BOLD);
    refresh();

    nodelay(stdscr, FALSE);
    int ch = getch();
    nodelay(stdscr, TRUE);
    return (ch == 'y' || ch == 'Y');
}

Project *editor_run(Project *p)
{
    while (1) {
        /* Check Ctrl+C signal */
        if (g_sigint_received) {
            g_sigint_received = 0;
            if (confirm_quit()) break;
        }

        g_editor.stream_clients = stream_clients();
        g_editor.stream_port    = stream_get_port();
        draw_all(p);

        int ch = getch();
        if (ch == ERR) { { struct timespec _ts={0,16000000L}; nanosleep(&_ts,NULL); }; continue; }

        /* Global quit */
        if (ch == KEY_CTRL_C) {
            if (confirm_quit()) {
                if (audio_is_playing()) audio_stop();
                break;
            }
            continue;
        }

        /* Global: ^F goes to file mode from anywhere */
        if (ch == KEY_CTRL_F) {
            editor_set_mode(MODE_FILE, p);
            continue;
        }

        switch (g_editor.mode) {
        case MODE_TRACK: handle_track_key(ch, p);  break;
        case MODE_EDIT:  handle_edit_key(ch, p);   break;
        case MODE_FILE:  handle_file_key(ch, &p);  break;
        default: break;
        }

        if (g_editor.exit_to_intro) break;
    }
    return p;
}
