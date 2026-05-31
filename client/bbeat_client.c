/*
 * bbeat_client.c – BashBeats PCM stream receiver and player
 *
 * Connects to a running BashBeats instance (default: localhost:9000)
 * and plays the raw 44100 Hz / 16-bit stereo PCM stream.
 *
 * Audio backends:
 *   Linux   → aplay (ALSA; install alsa-utils if missing)
 *   Windows → WinMM waveOut (built-in)
 *
 * UI: ANSI terminal, works on Linux and Windows 10+
 *
 * Threading model (anti-stutter):
 *   net_thread   : TCP recv → ring buffer (absorbs network jitter)
 *   audio_fn     : ring buffer → audio backend (steady drain rate)
 *   main thread  : UI + keyboard
 *
 * The ring buffer pre-fills ~185 ms before audio starts, providing
 * a jitter margin without noticeable startup delay.
 */

/* ─── Platform includes ────────────────────────────────────────────── */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <mmsystem.h>
#  include <conio.h>
typedef SOCKET sock_t;
#  define SOCK_NONE      INVALID_SOCKET
#  define SHUT_RDWR      SD_BOTH
#  define sock_close(s)  closesocket(s)
#  define sleep_ms(n)    Sleep(n)
static inline int  net_init(void)   { WSADATA w; return WSAStartup(MAKEWORD(2,2),&w) ? -1 : 0; }
static inline void net_deinit(void) { WSACleanup(); }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <errno.h>
#  include <termios.h>
#  include <sys/select.h>
#  include <sys/time.h>
typedef int sock_t;
#  define SOCK_NONE     (-1)
#  define sock_close(s)  close(s)
#  define sleep_ms(n)   do { struct timespec _t={0,(n)*1000000L}; nanosleep(&_t,NULL); } while(0)
#  define net_init()    do {} while(0)
#  define net_deinit()  do {} while(0)
#endif

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── PCM constants (must match BashBeats server) ──────────────────── */
#define PCM_RATE     44100
#define PCM_CHAN     2
#define PCM_BITS     16
#define PCM_FRAMESZ  (PCM_CHAN * (PCM_BITS/8))   /* 4 bytes/frame */

/* ─── Ring buffer (net thread → audio thread) ──────────────────────── */
/*
 * 256 KB ≈ 1.45 s of audio. The net thread writes here; the audio
 * thread drains it at the playback rate.  Pre-fill 32 KB (≈185 ms)
 * before starting audio to absorb network jitter.
 */
#define RING_BYTES   (1 << 18)   /* 262144 bytes */
#define RING_MASK    (RING_BYTES - 1)
#define PREBUF_BYTES (1 << 15)   /* 32768 bytes  */
#define AUDIO_CHUNK  (4096 * PCM_FRAMESZ)   /* 16384 bytes per audio write */

typedef struct {
    uint8_t         data[RING_BYTES];
    int             rd;
    int             wr;
    int             quit;
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
} Ring;

static Ring g_ring;

static void ring_init(void) {
    memset(&g_ring, 0, sizeof(g_ring));
    pthread_mutex_init(&g_ring.mtx, NULL);
    pthread_cond_init(&g_ring.cond, NULL);
}

static void ring_destroy(void) {
    pthread_mutex_destroy(&g_ring.mtx);
    pthread_cond_destroy(&g_ring.cond);
}

/* caller must hold g_ring.mtx */
static int ring_avail_locked(void) {
    int a = g_ring.wr - g_ring.rd;
    return a < 0 ? a + RING_BYTES : a;
}
static int ring_free_locked(void) {
    return RING_BYTES - 1 - ring_avail_locked();
}

/* Returns fill percentage 0..100 (lock-free approximate, for display) */
static int ring_fill_pct(void) {
    int a = ring_avail_locked();  /* racy but ok for display */
    return (int)((long long)a * 100 / RING_BYTES);
}

/* Write len bytes from data. Blocks if ring is full or until quit. */
static void ring_write(const uint8_t *data, size_t len) {
    while (len > 0) {
        pthread_mutex_lock(&g_ring.mtx);
        while (ring_free_locked() == 0 && !g_ring.quit)
            pthread_cond_wait(&g_ring.cond, &g_ring.mtx);
        if (g_ring.quit) { pthread_mutex_unlock(&g_ring.mtx); return; }

        size_t can = (size_t)ring_free_locked();
        if (can > len) can = len;

        size_t tail = (size_t)(RING_BYTES - g_ring.wr);
        if (can <= tail) {
            memcpy(g_ring.data + g_ring.wr, data, can);
        } else {
            memcpy(g_ring.data + g_ring.wr, data, tail);
            memcpy(g_ring.data, data + tail, can - tail);
        }
        g_ring.wr = (g_ring.wr + (int)can) & RING_MASK;
        data += can;
        len  -= can;

        pthread_cond_signal(&g_ring.cond);
        pthread_mutex_unlock(&g_ring.mtx);
    }
}

/*
 * Read exactly max bytes into buf (blocks until available) or until quit.
 * Waiting for a full chunk before each audio_write prevents small,
 * irregular writes to the audio backend that cause stuttering.
 * Returns 0 only when quit is set and the ring is empty.
 */
static size_t ring_read(uint8_t *buf, size_t max) {
    pthread_mutex_lock(&g_ring.mtx);
    /* Wait until the ring has a full chunk ready, or quit */
    while ((size_t)ring_avail_locked() < max && !g_ring.quit)
        pthread_cond_wait(&g_ring.cond, &g_ring.mtx);

    int avail = ring_avail_locked();
    if (avail == 0) { pthread_mutex_unlock(&g_ring.mtx); return 0; }

    size_t take = (size_t)avail < max ? (size_t)avail : max;

    size_t tail = (size_t)(RING_BYTES - g_ring.rd);
    if (take <= tail) {
        memcpy(buf, g_ring.data + g_ring.rd, take);
    } else {
        memcpy(buf, g_ring.data + g_ring.rd, tail);
        memcpy(buf + tail, g_ring.data, take - tail);
    }
    g_ring.rd = (g_ring.rd + (int)take) & RING_MASK;

    pthread_cond_signal(&g_ring.cond);
    pthread_mutex_unlock(&g_ring.mtx);
    return take;
}

/* Wake all threads waiting on ring (for clean shutdown). */
static void ring_quit_signal(void) {
    pthread_mutex_lock(&g_ring.mtx);
    g_ring.quit = 1;
    pthread_cond_broadcast(&g_ring.cond);
    pthread_mutex_unlock(&g_ring.mtx);
}

/* Reset ring state for a new connection. */
static void ring_reset(void) {
    pthread_mutex_lock(&g_ring.mtx);
    g_ring.rd   = 0;
    g_ring.wr   = 0;
    g_ring.quit = 0;
    pthread_mutex_unlock(&g_ring.mtx);
}

/* ─── Application state ────────────────────────────────────────────── */
typedef enum {
    ST_IDLE,
    ST_CONNECTING,
    ST_BUFFERING,   /* TCP connected, pre-filling ring before audio starts */
    ST_CONNECTED,   /* playing audio */
    ST_ERROR
} Status;

typedef struct {
    char            host[256];
    int             port;
    Status          status;
    char            errmsg[256];
    long long       bytes_rx;
    int             buf_pct;      /* ring fill 0..100, for display */
    volatile int    quit_flag;
    sock_t          sock;
    pthread_mutex_t lock;
    pthread_t       net_tid;
    int             net_live;
} App;

static App g_app;

/* ─── Terminal raw-mode helpers ────────────────────────────────────── */
#ifdef _WIN32
static void term_init(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  m = 0;
    if (GetConsoleMode(h, &m))
        SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
static void  term_cleanup(void) { }
static int   term_kbhit(void)   { return _kbhit(); }
static int   term_getkey(void)  {
    int c = _getch();
    if (c == 0 || c == 0xE0) _getch();
    return c;
}
#else
static struct termios g_orig_term;
static void term_init(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &g_orig_term);
    raw = g_orig_term;
    raw.c_lflag  &= ~(unsigned)(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}
static void term_cleanup(void) { tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term); }
static int  term_kbhit(void) {
    fd_set fds; struct timeval tv = {0,0};
    FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv) > 0;
}
static int  term_getkey(void) {
    unsigned char c;
    return (read(STDIN_FILENO, &c, 1) == 1) ? (int)c : -1;
}
#endif

/* ─── ANSI helpers ─────────────────────────────────────────────────── */
#define A_HOME     "\033[H"
#define A_CLEAR    "\033[2J"
#define A_EL       "\033[K"
#define A_RESET    "\033[0m"
#define A_BOLD     "\033[1m"
#define A_RED      "\033[31m"
#define A_GREEN    "\033[32m"
#define A_YELLOW   "\033[33m"
#define A_CYAN     "\033[36m"
#define A_HIDE_CUR "\033[?25l"
#define A_SHOW_CUR "\033[?25h"

/* ─── Platform audio ───────────────────────────────────────────────── */

/* ── Linux: pipe to aplay ────────────────────────────────────────── */
#if defined(__linux__)

static FILE *g_pipe;

/* -B 400000: 400 ms ALSA hardware buffer — extra headroom against underruns */
static int  audio_open(void)
    { g_pipe = popen("aplay -f S16_LE -r 44100 -c 2 -q -B 400000 - 2>/dev/null","w"); return g_pipe?0:-1; }
static void audio_write(const void *d, size_t n)
    { if (g_pipe) fwrite(d,1,n,g_pipe); }
static void audio_close(void)
    { if (g_pipe) { pclose(g_pipe); g_pipe=NULL; } }

/* ── Windows: WinMM waveOut ──────────────────────────────────────── */
#elif defined(_WIN32)

#define MM_NBUFS  8
#define MM_BUFSZ  AUDIO_CHUNK

typedef struct {
    HWAVEOUT hwave;
    WAVEHDR  hdr[MM_NBUFS];
    uint8_t  buf[MM_NBUFS][MM_BUFSZ];
    HANDLE   event;
    int      next;
    int      open;
} WinAudio;
static WinAudio g_wa;

static int audio_open(void) {
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = PCM_CHAN;
    fmt.nSamplesPerSec  = PCM_RATE;
    fmt.wBitsPerSample  = PCM_BITS;
    fmt.nBlockAlign     = (WORD)PCM_FRAMESZ;
    fmt.nAvgBytesPerSec = PCM_RATE * PCM_FRAMESZ;
    g_wa.event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&g_wa.hwave, WAVE_MAPPER, &fmt,
                    (DWORD_PTR)g_wa.event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
        return -1;
    for (int i = 0; i < MM_NBUFS; i++) {
        memset(&g_wa.hdr[i], 0, sizeof(WAVEHDR));
        g_wa.hdr[i].lpData         = (LPSTR)g_wa.buf[i];
        g_wa.hdr[i].dwBufferLength = MM_BUFSZ;
        waveOutPrepareHeader(g_wa.hwave, &g_wa.hdr[i], sizeof(WAVEHDR));
    }
    g_wa.open = 1;
    return 0;
}

static void audio_write(const void *data, size_t len) {
    if (!g_wa.open) return;
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        int      idx = g_wa.next;
        WAVEHDR *h   = &g_wa.hdr[idx];
        while (h->dwFlags & WHDR_INQUEUE)
            WaitForSingleObject(g_wa.event, 10);
        size_t chunk = len < MM_BUFSZ ? len : MM_BUFSZ;
        memcpy(g_wa.buf[idx], p, chunk);
        h->dwBufferLength = (DWORD)chunk;
        h->dwFlags       &= ~WHDR_DONE;
        waveOutWrite(g_wa.hwave, h, sizeof(WAVEHDR));
        p += chunk; len -= chunk;
        g_wa.next = (idx + 1) % MM_NBUFS;
    }
}

static void audio_close(void) {
    if (!g_wa.open) return;
    waveOutReset(g_wa.hwave);
    for (int i = 0; i < MM_NBUFS; i++)
        waveOutUnprepareHeader(g_wa.hwave, &g_wa.hdr[i], sizeof(WAVEHDR));
    waveOutClose(g_wa.hwave);
    CloseHandle(g_wa.event);
    g_wa.open = 0;
}

#else
static int  audio_open(void)              { fprintf(stderr,"[bbeat_client] No audio backend.\n"); return -1; }
static void audio_write(const void *d, size_t n) { (void)d; (void)n; }
static void audio_close(void)             { }
#endif /* platform audio */


/* ─── Audio thread ─────────────────────────────────────────────────── */
/*
 * Drains the ring buffer into the audio backend at playback rate.
 * Waits for PREBUF_BYTES to accumulate before opening the device,
 * so the backend never starves during the initial fill.
 */
static pthread_t g_audio_tid;

static void *audio_fn(void *arg) {
    (void)arg;

    /* Wait for pre-buffer fill (or disconnect signal) */
    pthread_mutex_lock(&g_ring.mtx);
    while (ring_avail_locked() < PREBUF_BYTES && !g_ring.quit)
        pthread_cond_wait(&g_ring.cond, &g_ring.mtx);
    pthread_mutex_unlock(&g_ring.mtx);

    if (g_ring.quit) return NULL;

    /* Mark status as actively playing */
    pthread_mutex_lock(&g_app.lock);
    if (g_app.status == ST_BUFFERING) g_app.status = ST_CONNECTED;
    pthread_mutex_unlock(&g_app.lock);

    if (audio_open() != 0) {
        pthread_mutex_lock(&g_app.lock);
        g_app.status = ST_ERROR;
        snprintf(g_app.errmsg, sizeof(g_app.errmsg), "Failed to open audio device");
        pthread_mutex_unlock(&g_app.lock);
        ring_quit_signal();   /* unblock any blocked ring_write in net_thread */
        return NULL;
    }

    static uint8_t chunk[AUDIO_CHUNK];
    for (;;) {
        size_t n = ring_read(chunk, sizeof(chunk));
        if (n == 0) break;   /* quit signalled and ring empty */
        audio_write(chunk, n);
    }

    audio_close();
    return NULL;
}


/* ─── Network thread ───────────────────────────────────────────────── */
static void *net_thread(void *arg) {
    (void)arg;
    App *a = &g_app;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)a->port);

    struct hostent *he = gethostbyname(a->host);
    if (!he) {
        pthread_mutex_lock(&a->lock);
        a->status = ST_ERROR;
        snprintf(a->errmsg, sizeof(a->errmsg), "Cannot resolve '%.220s'", a->host);
        pthread_mutex_unlock(&a->lock);
        a->net_live = 0;
        return NULL;
    }
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == SOCK_NONE) {
        pthread_mutex_lock(&a->lock);
        a->status = ST_ERROR;
        snprintf(a->errmsg, sizeof(a->errmsg), "socket() failed");
        pthread_mutex_unlock(&a->lock);
        a->net_live = 0;
        return NULL;
    }

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        sock_close(s);
        pthread_mutex_lock(&a->lock);
        a->status = ST_ERROR;
        snprintf(a->errmsg, sizeof(a->errmsg), "Cannot connect to %.200s:%d", a->host, a->port);
        pthread_mutex_unlock(&a->lock);
        a->net_live = 0;
        return NULL;
    }

    pthread_mutex_lock(&a->lock);
    a->sock   = s;
    a->status = ST_BUFFERING;
    pthread_mutex_unlock(&a->lock);

    /* Start audio thread; it will wait for pre-buffer internally */
    pthread_create(&g_audio_tid, NULL, audio_fn, NULL);

    /* Receive PCM → ring buffer */
    static uint8_t rx[4096 * PCM_FRAMESZ];
    while (!a->quit_flag) {
#ifdef _WIN32
        int n = recv(s, (char *)rx, (int)sizeof(rx), 0);
#else
        ssize_t n = recv(s, rx, sizeof(rx), 0);
#endif
        if (n <= 0) break;
        ring_write(rx, (size_t)n);
        pthread_mutex_lock(&a->lock);
        a->bytes_rx += n;
        a->buf_pct   = ring_fill_pct();
        pthread_mutex_unlock(&a->lock);
    }

    /* Signal audio thread to drain remaining ring data and stop */
    ring_quit_signal();
    pthread_join(g_audio_tid, NULL);

    sock_close(s);

    pthread_mutex_lock(&a->lock);
    a->sock    = SOCK_NONE;
    a->buf_pct = 0;
    if (!a->quit_flag) {
        a->status = ST_ERROR;
        snprintf(a->errmsg, sizeof(a->errmsg), "Connection closed by server");
    } else {
        a->status = ST_IDLE;
    }
    pthread_mutex_unlock(&a->lock);

    a->net_live = 0;
    return NULL;
}


/* ─── Connect / disconnect ─────────────────────────────────────────── */
static void do_connect(void) {
    if (g_app.net_live) return;
    ring_reset();
    g_app.quit_flag = 0;
    g_app.bytes_rx  = 0;
    g_app.buf_pct   = 0;
    g_app.errmsg[0] = '\0';
    g_app.status    = ST_CONNECTING;
    g_app.net_live  = 1;
    pthread_create(&g_app.net_tid, NULL, net_thread, NULL);
}

static void do_disconnect(void) {
    if (!g_app.net_live) return;
    g_app.quit_flag = 1;

    /* Unblock recv() */
    pthread_mutex_lock(&g_app.lock);
    sock_t s = g_app.sock;
    pthread_mutex_unlock(&g_app.lock);
    if (s != SOCK_NONE) shutdown(s, SHUT_RDWR);

    /* Unblock any ring_write / ring_read / prebuf wait */
    ring_quit_signal();

    pthread_join(g_app.net_tid, NULL);
    ring_reset();

    g_app.quit_flag = 0;
    g_app.status    = ST_IDLE;
}


/* ─── UI ────────────────────────────────────────────────────────────── */
static void draw_ui(Status st, const char *host, int port, long long rx,
                    int buf_pct, const char *errmsg, int editing, const char *ibuf)
{
    fputs(A_HOME A_HIDE_CUR, stdout);

    fputs(A_BOLD A_CYAN "  BashBeats Stream Client" A_RESET A_EL "\n\n", stdout);

    printf("  Host   : %s" A_EL "\n", host);
    printf("  Port   : %d" A_EL "\n\n", port);

    /* Status */
    const char *col, *label;
    switch (st) {
        case ST_IDLE:       col = A_RESET;  label = "Disconnected";  break;
        case ST_CONNECTING: col = A_YELLOW; label = "Connecting..."; break;
        case ST_BUFFERING:  col = A_YELLOW; label = "Buffering...";  break;
        case ST_CONNECTED:  col = A_GREEN;  label = "Connected";     break;
        case ST_ERROR:      col = A_RED;    label = "Error";         break;
        default:            col = A_RESET;  label = "?";             break;
    }
    printf("  Status : %s%s%s" A_EL "\n", col, label, A_RESET);

    if (st == ST_ERROR && errmsg[0])
        printf("  Reason : " A_RED "%s" A_RESET A_EL "\n", errmsg);
    else
        fputs(A_EL "\n", stdout);

    /* Buffer fill bar (shown while buffering or playing) */
    if (st == ST_BUFFERING || st == ST_CONNECTED) {
        char bar[21];
        int  filled = buf_pct * 20 / 100;
        for (int i = 0; i < 20; i++) bar[i] = (i < filled) ? '#' : '.';
        bar[20] = '\0';
        printf("  Buffer : [%s] %d%%" A_EL "\n", bar, buf_pct);
    } else {
        fputs(A_EL "\n", stdout);
    }

    /* Bytes received */
    if (rx < 1024)
        printf("  Recv   : %lld B" A_EL "\n\n", rx);
    else if (rx < 1024*1024)
        printf("  Recv   : %.1f KB" A_EL "\n\n", rx/1024.0);
    else
        printf("  Recv   : %.2f MB" A_EL "\n\n", rx/(1024.0*1024.0));

    if (editing) {
        printf("  Edit host:port (Enter: confirm  Esc: cancel)" A_EL "\n");
        printf("  > %s" A_EL A_SHOW_CUR, ibuf);
    } else {
        printf("  " A_BOLD "[C]" A_RESET "onnect  "
               A_BOLD "[D]" A_RESET "isconnect  "
               A_BOLD "[E]" A_RESET "dit address  "
               A_BOLD "[Q]" A_RESET "uit" A_EL "\n");
    }
    fflush(stdout);
}


/* ─── Signal handler ────────────────────────────────────────────────── */
static volatile int g_sig = 0;
static void on_signal(int s) { (void)s; g_sig = 1; }


/* ─── main ──────────────────────────────────────────────────────────── */
int main(void) {
    signal(SIGINT, on_signal);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    net_init();
    ring_init();
    memset(&g_app, 0, sizeof(g_app));
    strncpy(g_app.host, "localhost", sizeof(g_app.host)-1);
    g_app.port   = 9000;
    g_app.status = ST_IDLE;
    g_app.sock   = SOCK_NONE;
    pthread_mutex_init(&g_app.lock, NULL);

    term_init();
    fputs(A_CLEAR, stdout);

    int  editing = 0;
    char ibuf[256] = "";
    int  ilen = 0, icur = 0;

    while (!g_sig) {
        pthread_mutex_lock(&g_app.lock);
        Status    st  = g_app.status;
        int       pt  = g_app.port;
        int       bp  = g_app.buf_pct;
        long long rx  = g_app.bytes_rx;
        char      h[256], em[256];
        strncpy(h,  g_app.host,   sizeof(h)-1);  h[sizeof(h)-1]  = '\0';
        strncpy(em, g_app.errmsg, sizeof(em)-1); em[sizeof(em)-1] = '\0';
        pthread_mutex_unlock(&g_app.lock);

        draw_ui(st, h, pt, rx, bp, em, editing, ibuf);

        if (!term_kbhit()) { sleep_ms(50); continue; }

        int ch = term_getkey();
        if (ch < 0) continue;

        if (editing) {
            if (ch == '\n' || ch == '\r') {
                char tmp[256];
                strncpy(tmp, ibuf, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
                char *col = strrchr(tmp, ':');
                if (col) {
                    int p = atoi(col+1);
                    if (p > 0 && p < 65536) {
                        pthread_mutex_lock(&g_app.lock);
                        g_app.port = p;
                        pthread_mutex_unlock(&g_app.lock);
                    }
                    *col = '\0';
                }
                if (tmp[0]) {
                    pthread_mutex_lock(&g_app.lock);
                    strncpy(g_app.host, tmp, sizeof(g_app.host)-1);
                    pthread_mutex_unlock(&g_app.lock);
                }
                editing = 0; ilen = 0; icur = 0; ibuf[0] = '\0';
            } else if (ch == 27) {
                editing = 0; ilen = 0; icur = 0; ibuf[0] = '\0';
            } else if (ch == 127 || ch == 8) {
                if (icur > 0) {
                    memmove(ibuf+icur-1, ibuf+icur, (size_t)(ilen-icur+1));
                    icur--; ilen--;
                }
            } else if (ch >= 32 && ch < 127 && ilen < 253) {
                memmove(ibuf+icur+1, ibuf+icur, (size_t)(ilen-icur+1));
                ibuf[icur] = (char)ch;
                icur++; ilen++;
            } else if (ch == 3) {
                g_sig = 1;
            }
        } else {
            switch (ch) {
            case 'c': case 'C': do_connect();    break;
            case 'd': case 'D': do_disconnect(); break;
            case 'e': case 'E':
                editing = 1;
                pthread_mutex_lock(&g_app.lock);
                snprintf(ibuf, sizeof(ibuf), "%.240s:%d", g_app.host, g_app.port);
                pthread_mutex_unlock(&g_app.lock);
                ilen = (int)strlen(ibuf);
                icur = ilen;
                break;
            case 'q': case 'Q': case 3:
                g_sig = 1;
                break;
            }
        }
    }

    do_disconnect();
    term_cleanup();
    ring_destroy();
    fputs(A_RESET A_SHOW_CUR "\n", stdout);
    pthread_mutex_destroy(&g_app.lock);
    net_deinit();
    return 0;
}
