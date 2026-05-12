#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include "ring.h"
#include "session.h"
#include "synth.h"
#include "wav.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#define C_RESET "\033[0m"
#define C_DIM   "\033[2m"
#define C_BOLD  "\033[1m"
#define C_CYAN  "\033[96m"
#define C_BLUE  "\033[94m"
#define C_GREEN "\033[92m"
#define C_YELL  "\033[93m"
#define C_MAG   "\033[95m"
#define C_RED   "\033[91m"
#define C_INV   "\033[7m"

static volatile sig_atomic_t g_running = 1;
static struct termios g_old_term;
static int g_have_term = 0;

static const char *track_names[BB_TRACKS] = {
    "KICK ", "SNARE", "HAT  ", "C4   ", "D4   ", "E4   ", "F4   ", "G4   ", "A4   ", "B4   "
};
static const char *track_icon[BB_TRACKS] = {"●", "◆", "✦", "♪", "♪", "♪", "♪", "♪", "♪", "♪"};

typedef struct {
    Engine engine;
    AudioRing ring;
    int port;
    int listen_fd;
    pthread_mutex_t client_lock;
    int client_fd;
    volatile int running;
} App;

static void on_signal(int sig) { (void)sig; g_running = 0; }

static void restore_terminal(void) {
    if (g_have_term) tcsetattr(STDIN_FILENO, TCSANOW, &g_old_term);
    printf("\033[?25h\033[0m\n");
}

static void setup_terminal(void) {
    if (tcgetattr(STDIN_FILENO, &g_old_term) == 0) {
        struct termios raw = g_old_term;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_have_term = 1;
        atexit(restore_terminal);
    }
    printf("\033[?25l");
}

static int make_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); close(fd); return -1; }
    if (listen(fd, 8) < 0) { perror("listen"); close(fd); return -1; }
    return fd;
}

static void print_cell(const char *s, int selected, int live, int on) {
    if (selected) printf(C_INV C_BOLD "%3s " C_RESET, s);
    else if (live && on) printf(C_GREEN C_BOLD "%3s " C_RESET, s);
    else if (live) printf(C_CYAN C_BOLD "%3s " C_RESET, "│");
    else if (on) printf(C_YELL "%3s " C_RESET, s);
    else printf(C_DIM "%3s " C_RESET, "·");
}

static void draw_bar(void) {
    printf(C_DIM "            ");
    for (int i = 0; i < BB_STEPS; ++i) printf(i % 4 == 0 ? "╷   " : "    ");
    printf(C_RESET "\n");
}

static void draw_ui(App *app, const char *status) {
    Session s;
    int step, sel_t, sel_s, playing;
    engine_snapshot(&app->engine, &s, &step, &sel_t, &sel_s, &playing);
    pthread_mutex_lock(&app->client_lock);
    int connected = app->client_fd >= 0;
    pthread_mutex_unlock(&app->client_lock);

    printf("\033[H\033[J");
    printf(C_CYAN "╔════════════════════════════════════════════════════════════════════════════════╗\n" C_RESET);
    printf(C_CYAN "║ " C_RESET C_BOLD "BashBeats" C_RESET "  realtime terminal DAW   "
           C_DIM "port:" C_RESET C_YELL "%d" C_RESET "   "
           C_DIM "client:" C_RESET "%s   "
           C_DIM "audio-step:" C_RESET C_CYAN "%02d" C_RESET
           C_CYAN "                         ║\n" C_RESET,
           app->port, connected ? C_GREEN "connected" C_RESET : C_RED "waiting" C_RESET, step);
    printf(C_CYAN "╚════════════════════════════════════════════════════════════════════════════════╝\n" C_RESET);

    printf(C_BLUE "┌─ STATUS ────────────────────────────────────────────────────────────────────┐\n" C_RESET);
    printf(C_BLUE "│" C_RESET " BPM " C_YELL C_BOLD "%3d" C_RESET "   transport %s   selected " C_MAG "%s" C_RESET " step " C_CYAN "%02d" C_RESET
           "   vol %.2f   pitch %.2fx" C_BLUE " │\n" C_RESET,
           s.bpm, playing ? C_GREEN C_BOLD "PLAY " C_RESET : C_RED C_BOLD "PAUSE" C_RESET,
           track_names[sel_t], sel_s, s.volume[sel_t], s.pitch[sel_t]);
    printf(C_BLUE "├─ CONTROLS ──────────────────────────────────────────────────────────────────┤\n" C_RESET);
    printf(C_BLUE "│" C_RESET " ←/→ step  ↑/↓ track  x/1-0 toggle  +/- BPM  space play  v/V vol  p/P pitch  s/l save/load  e export  q quit " C_BLUE "│\n" C_RESET);
    printf(C_BLUE "└──────────────────────────────────────────────────────────────────────────────┘\n" C_RESET);

    printf("\n" C_DIM "            ");
    for (int i = 0; i < BB_STEPS; ++i) printf("%3d ", i);
    printf(C_RESET "\n");
    draw_bar();

    for (int t = 0; t < BB_TRACKS; ++t) {
        int is_piano = t >= BB_DRUM_TRACKS;
        if (t == BB_DRUM_TRACKS) {
            printf(C_DIM "            ─────────────── one octave piano roll ───────────────\n" C_RESET);
        }
        printf("%s%s%s %-5s " C_DIM "v" C_RESET "%.2f ",
               t == sel_t ? C_INV C_BOLD : (is_piano ? C_MAG : C_RESET),
               track_icon[t], track_names[t], t == sel_t ? C_RESET : "", s.volume[t]);
        for (int i = 0; i < BB_STEPS; ++i) {
            int cursor = (t == sel_t && i == sel_s);
            int live = (i == step);
            const char *mark = s.pattern[t][i] ? track_icon[t] : "·";
            print_cell(mark, cursor, live, s.pattern[t][i]);
        }
        printf("\n");
    }
    printf("\n" C_DIM "Tip: WSL playback -> ./bashbeats_client 127.0.0.1 7777 --stdout | aplay -q -f S16_LE -c 1 -r 44100 -B 40000 -F 10000" C_RESET "\n");
    printf(C_GREEN "%s" C_RESET "\n", status ? status : "ready");
    fflush(stdout);
}

static int export_wav(App *app, const char *path) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) return -1;
    int seconds = 16;
    uint32_t frames = BB_SAMPLE_RATE * seconds;
    uint32_t data_bytes = frames * 2;
    uint32_t riff_size = 36 + data_bytes;
    unsigned char h[44] = {0};
    memcpy(h, "RIFF", 4); memcpy(h+8, "WAVEfmt ", 8); memcpy(h+36, "data", 4);
    *(uint32_t*)(h+4)=riff_size; *(uint32_t*)(h+16)=16; *(uint16_t*)(h+20)=1; *(uint16_t*)(h+22)=1;
    *(uint32_t*)(h+24)=BB_SAMPLE_RATE; *(uint32_t*)(h+28)=BB_SAMPLE_RATE*2; *(uint16_t*)(h+32)=2; *(uint16_t*)(h+34)=16; *(uint32_t*)(h+40)=data_bytes;
    if (bb_write_all(fd, h, 44) < 0) { close(fd); return -1; }
    int16_t block[BB_BLOCK_FRAMES];
    for (uint32_t done=0; done<frames; done += BB_BLOCK_FRAMES) {
        int n = (frames-done > BB_BLOCK_FRAMES) ? BB_BLOCK_FRAMES : (int)(frames-done);
        engine_render_block(&app->engine, block, n);
        if (bb_write_all(fd, block, (size_t)n*2) < 0) { close(fd); return -1; }
    }
    close(fd);
    return 0;
}

static void *audio_thread(void *arg) {
    App *app = (App *)arg;
    int16_t block[BB_BLOCK_FRAMES];
    uint64_t period_ns = (uint64_t)((1000000000.0 * BB_BLOCK_FRAMES) / BB_SAMPLE_RATE);
    uint64_t deadline = bb_now_mono_ns();
    while (app->running && g_running) {
        engine_render_block(&app->engine, block, BB_BLOCK_FRAMES);
        if (!ring_push(&app->ring, block)) break;
        deadline += period_ns;
        bb_sleep_until_ns(deadline);
    }
    ring_close(&app->ring);
    return NULL;
}

static void close_client(App *app) {
    pthread_mutex_lock(&app->client_lock);
    if (app->client_fd >= 0) close(app->client_fd);
    app->client_fd = -1;
    pthread_mutex_unlock(&app->client_lock);
}

static void *stream_thread(void *arg) {
    App *app = (App *)arg;
    int16_t block[BB_BLOCK_FRAMES];
    while (app->running && g_running && ring_pop(&app->ring, block)) {
        pthread_mutex_lock(&app->client_lock);
        int fd = app->client_fd;
        pthread_mutex_unlock(&app->client_lock);
        if (fd < 0) continue;
        if (bb_write_all(fd, block, sizeof(block)) < 0) close_client(app);
    }
    return NULL;
}

static void *accept_thread(void *arg) {
    App *app = (App *)arg;
    while (app->running && g_running) {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int cfd = accept(app->listen_fd, (struct sockaddr *)&peer, &len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { bb_sleep_ms(30); continue; }
            break;
        }
        pthread_mutex_lock(&app->client_lock);
        if (app->client_fd >= 0) close(app->client_fd);
        app->client_fd = cfd;
        pthread_mutex_unlock(&app->client_lock);
    }
    return NULL;
}

static int read_key(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n != 1) return 0;
    if (c == 27) { /* arrows: ESC [ A/B/C/D */
        unsigned char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        if (seq[0] == '[') {
            if (seq[1] == 'A') return 1001;
            if (seq[1] == 'B') return 1002;
            if (seq[1] == 'C') return 1003;
            if (seq[1] == 'D') return 1004;
        }
        return 27;
    }
    return c;
}

static void *input_thread(void *arg) {
    App *app = (App *)arg;
    char status[256] = "ready";
    draw_ui(app, status);
    while (app->running && g_running) {
        int k = read_key();
        if (k) {
            pthread_mutex_lock(&app->engine.lock);
            int st = app->engine.selected_step;
            int tr = app->engine.selected_track;
            pthread_mutex_unlock(&app->engine.lock);

            if (k == 'q') { app->running = 0; g_running = 0; snprintf(status, sizeof(status), "quit"); }
            else if (k == '+' || k == '=') { engine_adjust_bpm(&app->engine, 5); snprintf(status, sizeof(status), "BPM +5"); }
            else if (k == '-' || k == '_') { engine_adjust_bpm(&app->engine, -5); snprintf(status, sizeof(status), "BPM -5"); }
            else if (k == ' ') { pthread_mutex_lock(&app->engine.lock); app->engine.playing = !app->engine.playing; pthread_mutex_unlock(&app->engine.lock); snprintf(status, sizeof(status), "play/pause toggled"); }
            else if (k == 1004 || k == '[') { pthread_mutex_lock(&app->engine.lock); app->engine.selected_step = (st + BB_STEPS - 1) % BB_STEPS; pthread_mutex_unlock(&app->engine.lock); }
            else if (k == 1003 || k == ']') { pthread_mutex_lock(&app->engine.lock); app->engine.selected_step = (st + 1) % BB_STEPS; pthread_mutex_unlock(&app->engine.lock); }
            else if (k == 1001) { pthread_mutex_lock(&app->engine.lock); app->engine.selected_track = (tr + BB_TRACKS - 1) % BB_TRACKS; pthread_mutex_unlock(&app->engine.lock); }
            else if (k == 1002 || k == '\t') { pthread_mutex_lock(&app->engine.lock); app->engine.selected_track = (tr + 1) % BB_TRACKS; pthread_mutex_unlock(&app->engine.lock); }
            else if (k >= '1' && k <= '9') { int idx = k - '1'; if (idx < BB_TRACKS) engine_toggle_step(&app->engine, idx, st); snprintf(status, sizeof(status), "toggle %s step %02d", track_names[idx], st); }
            else if (k == '0') { engine_toggle_step(&app->engine, 9, st); snprintf(status, sizeof(status), "toggle %s step %02d", track_names[9], st); }
            else if (k == 'x') { engine_toggle_step(&app->engine, tr, st); snprintf(status, sizeof(status), "toggle %s step %02d", track_names[tr], st); }
            else if (k == 'v') { engine_adjust_volume(&app->engine, tr, -0.05f); snprintf(status, sizeof(status), "volume down"); }
            else if (k == 'V') { engine_adjust_volume(&app->engine, tr, 0.05f); snprintf(status, sizeof(status), "volume up"); }
            else if (k == 'p') { engine_adjust_pitch(&app->engine, tr, -0.05f); snprintf(status, sizeof(status), "pitch down"); }
            else if (k == 'P') { engine_adjust_pitch(&app->engine, tr, 0.05f); snprintf(status, sizeof(status), "pitch up"); }
            else if (k == 's') { Session s; engine_snapshot(&app->engine, &s, NULL, NULL, NULL, NULL); snprintf(status, sizeof(status), session_save("session.daw", &s) == 0 ? "saved session.daw" : "save failed"); }
            else if (k == 'l') { Session s; if (session_load("session.daw", &s) == 0) { pthread_mutex_lock(&app->engine.lock); app->engine.session = s; pthread_mutex_unlock(&app->engine.lock); snprintf(status, sizeof(status), "loaded session.daw"); } else snprintf(status, sizeof(status), "load failed"); }
            else if (k == 'e') { snprintf(status, sizeof(status), export_wav(app, "export.wav") == 0 ? "exported export.wav" : "export failed"); }
            draw_ui(app, status);
        } else {
            draw_ui(app, status);
            bb_sleep_ms(50);
        }
    }
    ring_close(&app->ring);
    if (app->listen_fd >= 0) close(app->listen_fd);
    close_client(app);
    return NULL;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s [--port 7777] kick.wav snare.wav hat.wav piano.wav\n", argv0);
}

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int port = BB_DEFAULT_PORT;
    int first_sample = 1;
    if (argc >= 3 && strcmp(argv[1], "--port") == 0) {
        port = atoi(argv[2]); first_sample = 3;
    }
    if (argc - first_sample < 4) { usage(argv[0]); return 1; }

    App app;
    memset(&app, 0, sizeof(app));
    app.port = port;
    app.client_fd = -1;
    app.listen_fd = -1;
    app.running = 1;
    pthread_mutex_init(&app.client_lock, NULL);
    ring_init(&app.ring);
    engine_init(&app.engine);

    if (wav_load_mono16(argv[first_sample + 0], &app.engine.samples[0]) < 0) return 1;
    if (wav_load_mono16(argv[first_sample + 1], &app.engine.samples[1]) < 0) return 1;
    if (wav_load_mono16(argv[first_sample + 2], &app.engine.samples[2]) < 0) return 1;
    for (int t = BB_DRUM_TRACKS; t < BB_TRACKS; ++t) {
        if (wav_load_mono16(argv[first_sample + 3], &app.engine.samples[t]) < 0) return 1;
    }

    app.listen_fd = make_server_socket(port);
    if (app.listen_fd < 0) return 1;
    bb_set_nonblocking(app.listen_fd);
    setup_terminal();

    pthread_t ta, ti, ts, tc;
    pthread_create(&ta, NULL, audio_thread, &app);
    pthread_create(&tc, NULL, accept_thread, &app);
    pthread_create(&ts, NULL, stream_thread, &app);
    pthread_create(&ti, NULL, input_thread, &app);

    pthread_join(ti, NULL);
    app.running = 0;
    ring_close(&app.ring);
    if (app.listen_fd >= 0) close(app.listen_fd);
    close_client(&app);
    pthread_join(ta, NULL);
    pthread_join(tc, NULL);
    pthread_join(ts, NULL);

    engine_destroy(&app.engine);
    ring_destroy(&app.ring);
    pthread_mutex_destroy(&app.client_lock);
    return 0;
}
