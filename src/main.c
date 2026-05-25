#define _POSIX_C_SOURCE 200809L
#include "data.h"
#include "audio.h"
#include "editor.h"
#include "perform.h"
#include "file_io.h"
#include "input.h"
#include "stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <ncurses.h>

#define STREAM_PORT 9000

/* ── Global quit-request flag (set by SIGINT handler) ── */
volatile sig_atomic_t g_sigint_received = 0;

static void sigint_handler(int sig)
{
    (void)sig;
    g_sigint_received = 1;
}

static Project *g_project = NULL;
static int g_cleanup_registered = 0;

static void cleanup(void);

static void register_cleanup_once(void)
{
    if (!g_cleanup_registered) {
        atexit(cleanup);
        g_cleanup_registered = 1;
    }
}

static void cleanup(void)
{
    editor_cleanup();
    audio_cleanup();
    stream_cleanup();
    project_free(g_project);
    g_project = NULL;
}

/* ── ASCII art banner ────────────────────────────────────────────────
 * 5-row block letters, 7-bit ASCII, ~72 cols wide, fits 140 cols.
 * ─────────────────────────────────────────────────────────────────── */
static const char *BANNER[] = {
    "",
    "   ####     ###    #####   #   #   ####    #####    ###    #####   #####",
    "   #   #   #   #   #       #   #   #   #   #       #   #     #     #   ",
    "   ####    #####   ####    #####   ####    ###     #####     #     #### ",
    "   #   #   #   #       #   #   #   #   #   #       #   #     #         #",
    "   ####    #   #   ####    #   #   ####    #####   #   #     #     #### ",
    "",
    "                    CLI  Digital  Audio  Workstation",
    "",
    NULL
};

/* ── Fuzzy substring match ── */
static int str_contains(const char *hay, const char *needle)
{
    if (!needle[0]) return 1;
    int hl = (int)strlen(hay), nl = (int)strlen(needle);
    for (int i = 0; i <= hl - nl; i++) {
        int ok = 1;
        for (int j = 0; j < nl && ok; j++) {
            char a = hay[i+j], b = needle[j];
            if (a>='A'&&a<='Z') a+=32;
            if (b>='A'&&b<='Z') b+=32;
            if (a != b) ok = 0;
        }
        if (ok) return 1;
    }
    return 0;
}

static int confirm_intro_quit(void)
{
    int pr = LINES / 2 - 1;
    int pc = COLS / 2 - 18;
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(pr,   pc, "+------------------------------------+");
    mvprintw(pr+1, pc, "|  Quit BashBeats?  (y)es  (n)o      |");
    mvprintw(pr+2, pc, "+------------------------------------+");
    attroff(COLOR_PAIR(4) | A_BOLD);
    refresh();

    int ch = getch();
    return (ch == 'y' || ch == 'Y');
}

/* ── Intro screen ─────────────────────────────────────────────────────
 * Returns:
 *   0 = new project (DAW)
 *   1 = loaded project (DAW, g_project is set)
 *   2 = performance mode
 * ──────────────────────────────────────────────────────────────────── */
static int show_intro(void)
{
    /* Resize the terminal window BEFORE ncurses takes over.
     * \033[8;<rows>;<cols>t is the xterm "resize window" sequence.
     * Works in xterm, most modern terminal emulators, and SSH sessions
     * that pass through resize requests. */
    printf("\033[8;35;140t");
    fflush(stdout);
    /* Small pause so the terminal can process the resize */
    struct timespec ts = {0, 80000000L}; /* 80 ms */
    nanosleep(&ts, NULL);

    /* Init a minimal ncurses session just for the intro */
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN,   -1);
        init_pair(2, COLOR_GREEN,  -1);
        init_pair(3, COLOR_YELLOW, -1);
        init_pair(4, COLOR_WHITE,  -1);
        init_pair(8, COLOR_BLACK,  COLOR_CYAN);
    }

    resizeterm(35, 140);

    /* ── Collect .bbeat files ── */
    char saves[128][512];
    int  nsaves = 0;
    DIR *dp = opendir(SAVES_DIR);
    if (dp) {
        struct dirent *ent;
        while ((ent = readdir(dp)) && nsaves < 128) {
            size_t l = strlen(ent->d_name);
            if (l > 6 && strcmp(ent->d_name + l - 6, ".bbeat") == 0) {
                snprintf(saves[nsaves], sizeof(saves[nsaves]), "%s/%s", SAVES_DIR, ent->d_name);
                nsaves++;
            }
        }
        closedir(dp);
    }

    /* Menu:  0 = "New project",  1..nsaves = open saves[i-1] */
    int sel         = 0;
    char query[64]  = {0};
    int  result     = 0;
    int  done       = 0;

    while (!done) {
        erase();

        /* Banner */
        int brow = 1;
        for (int i = 0; BANNER[i]; i++, brow++) {
            attron(COLOR_PAIR(1) | A_BOLD);
            mvprintw(brow, 0, "%s", BANNER[i]);
            attroff(COLOR_PAIR(1) | A_BOLD);
        }

        /* Separator */
        attron(A_DIM);
        mvhline(brow, 0, ACS_HLINE, COLS);
        attroff(A_DIM);
        brow++;

        /* Filter box */
        attron(COLOR_PAIR(3));
        mvprintw(brow, 2, "Search: [%-28s]  (Up/Dn:move  Enter:open  Ctrl+C:quit)", query);
        attroff(COLOR_PAIR(3));
        brow++;

        /* Build filtered list:
         * Index 0: [+] New project (DAW)
         * Index 1: [~] Performance mode
         * Index 2+: .bbeat files */
        int fidx[128]; int nf = 0;
        int show_new  = str_contains("New project",      query) || !query[0];
        int show_perf = str_contains("Performance mode", query) || !query[0];

        for (int i = 0; i < nsaves; i++) {
            const char *sl = strrchr(saves[i], '/');
            const char *nm = sl ? sl+1 : saves[i];
            if (str_contains(nm, query)) fidx[nf++] = i;
        }

        int visible_count = (show_new ? 1 : 0) + (show_perf ? 1 : 0) + nf;
        if (sel >= visible_count && visible_count > 0) sel = visible_count - 1;
        if (sel < 0) sel = 0;

        int item_row = brow + 1;
        int vsel = 0;

        if (show_new) {
            int is_s = (vsel == sel);
            if (is_s) { attron(COLOR_PAIR(2)|A_BOLD); mvprintw(item_row, 4, "> [+] New project          (open DAW editor)"); attrset(A_NORMAL); }
            else       { attron(COLOR_PAIR(4));        mvprintw(item_row, 4, "  [+] New project          (open DAW editor)"); attrset(A_NORMAL); }
            item_row++; vsel++;
        }
        if (show_perf) {
            int is_s = (vsel == sel);
            if (is_s) { attron(COLOR_PAIR(5)|A_BOLD); mvprintw(item_row, 4, "> [~] Performance mode     (live keyboard play)"); attrset(A_NORMAL); }
            else       { attron(COLOR_PAIR(4));        mvprintw(item_row, 4, "  [~] Performance mode     (live keyboard play)"); attrset(A_NORMAL); }
            item_row++; vsel++;
        }
        for (int i = 0; i < nf; i++, vsel++) {
            const char *sl = strrchr(saves[fidx[i]], '/');
            const char *nm = sl ? sl+1 : saves[fidx[i]];
            int is_s = (vsel == sel);
            if (is_s) { attron(COLOR_PAIR(3)|A_BOLD); mvprintw(item_row, 4, "> %-62s", nm); attrset(A_NORMAL); }
            else       { attron(COLOR_PAIR(4));        mvprintw(item_row, 4, "  %-62s", nm); attrset(A_NORMAL); }
            item_row++;
        }
        if (visible_count == 0)
            mvprintw(item_row, 4, "(no matches)");

        /* Footer */
        attron(A_REVERSE);
        mvhline(LINES - 1, 0, ' ', COLS);
        mvprintw(LINES - 1, 1, " Up/Down: navigate   Enter: open   Type to filter   Ctrl+C: quit");
        attroff(A_REVERSE);

        refresh();

        /* Input */
        int ch = getch();

        if (g_sigint_received || ch == KEY_CTRL_C) {
            g_sigint_received = 0;
            if (confirm_intro_quit()) {
                endwin();
                exit(0);
            }
            continue;
        }

        if (ch == KEY_UP   && sel > 0) { sel--; continue; }
        if (ch == KEY_DOWN && sel < visible_count - 1) { sel++; continue; }

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (visible_count == 0) continue;
            int cur = 0;
            if (show_new)  { if (cur == sel) { result = 0; done = 1; break; } cur++; }
            if (show_perf) { if (cur == sel) { result = 2; done = 1; break; } cur++; }
            for (int i = 0; i < nf; i++, cur++) {
                if (cur == sel) {
                    Project *loaded = project_load(saves[fidx[i]]);
                    if (loaded) {
                        project_free(g_project);
                        g_project = loaded;
                        result = 1;
                        done = 1;
                    } else {
                        attron(COLOR_PAIR(4)|A_BOLD);
                        mvprintw(LINES-2, 2, "Load FAILED: %s  (press any key)", saves[fidx[i]]);
                        attroff(COLOR_PAIR(4)|A_BOLD);
                        refresh(); getch();
                    }
                    break;
                }
            }
            if (done) break;
        }

        /* Backspace */
        if (ch == KEY_BACKSPACE || ch == 127) {
            int ql = (int)strlen(query);
            if (ql > 0) { query[ql-1] = '\0'; sel = 0; }
            continue;
        }

        /* Printable char → filter */
        if (ch >= 32 && ch < 127 && (int)strlen(query) < 63) {
            query[strlen(query)] = (char)ch;
            sel = 0;
        }
    }

    endwin();
    return result;
}

/* ════════════════════════════════════════════════
 * ════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);   /* Ctrl+C → set flag, not hard exit */

    file_ensure_saves_dir();
    file_ensure_stub_instrument();

    if (argc >= 2) {
        /* Direct file argument: skip intro, go straight to DAW */
        g_project = project_load(argv[1]);
        if (!g_project) {
            fprintf(stderr, "BashBeats: failed to load '%s'\n", argv[1]);
            return 1;
        }
        goto launch_daw;
    }

intro_loop:
    /* ── Intro loop ────────────────────────────────────────────────
     * Re-enters when user presses ESC from performance mode.
     * Breaks out when user picks New / Load (goes to DAW).
     * ────────────────────────────────────────────────────────────── */
    {
        int perf_ready = 0;  /* 1 after first perf-mode init */
        while (1) {
            int choice = show_intro();

            if (choice == 2) {
                /* Performance mode */
                if (!perf_ready) {
                    register_cleanup_once();
                    if (audio_init(SAMPLES_DIR) != 0)
                        fprintf(stderr, "audio_init failed.\n");
                    if (!g_project)
                        g_project = project_new(NULL); /* dummy for cleanup */
                    editor_init(g_project);         /* colour pairs etc. */
                    perf_ready = 1;
                }
                run_performance_mode();  /* returns when user presses ESC */
                continue;               /* back to intro */
            }

            /* New project */
            if (choice == 0) {
                project_free(g_project);
                g_project = NULL;
            }
            if (g_project == NULL) {
                g_project = project_new(NULL);
                if (!g_project) {
                    fprintf(stderr, "BashBeats: failed to create project.\n");
                    return 1;
                }
            }
            /* choice == 1: g_project already loaded by show_intro */
            break;
        }
    }

launch_daw:
    register_cleanup_once();

    if (audio_init(SAMPLES_DIR) != 0)
        fprintf(stderr, "audio_init failed — continuing with stub.\n");

    audio_set_bpm(g_project->bpm);
    for (int i = 0; i < g_project->track_count; i++)
        audio_load_instrument(i, g_project->tracks[i].instrument);

    if (stream_init(STREAM_PORT) != 0)
        fprintf(stderr, "stream_init failed — streaming disabled.\n");

    editor_init(g_project);

    {
        extern EditorState g_editor;
        if (argc >= 2) {
            strncpy(g_editor.file_path, argv[1], sizeof(g_editor.file_path)-1);
            g_editor.file_path[sizeof(g_editor.file_path)-1] = '\0';
        } else {
            file_default_path(g_project->title, g_editor.file_path,
                              sizeof(g_editor.file_path));
        }
    }

    g_project = editor_run(g_project);
    if (g_editor.exit_to_intro) {
        audio_stop();
        stream_cleanup();
        editor_cleanup();
        goto intro_loop;
    }
    return 0;
}
