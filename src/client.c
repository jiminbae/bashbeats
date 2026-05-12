#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

static volatile sig_atomic_t g_running = 1;
static void on_signal(int sig) { (void)sig; g_running = 0; }

static int connect_to_server(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) { fprintf(stderr, "invalid IPv4 address: %s\n", host); close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); close(fd); return -1; }
    return fd;
}

#ifdef HAVE_ALSA
static snd_pcm_t *open_alsa(void) {
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) { fprintf(stderr, "ALSA open failed: %s\n", snd_strerror(err)); return NULL; }
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, BB_CHANNELS, BB_SAMPLE_RATE, 1, 50000);
    if (err < 0) { fprintf(stderr, "ALSA params failed: %s\n", snd_strerror(err)); snd_pcm_close(pcm); return NULL; }
    return pcm;
}

static void play_alsa(snd_pcm_t *pcm, const int16_t *buf, int frames) {
    int done = 0;
    while (done < frames && g_running) {
        snd_pcm_sframes_t n = snd_pcm_writei(pcm, buf + done, (snd_pcm_uframes_t)(frames - done));
        if (n == -EPIPE) { snd_pcm_prepare(pcm); continue; }
        if (n < 0) { snd_pcm_recover(pcm, (int)n, 1); continue; }
        done += (int)n;
    }
}
#endif

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s HOST PORT [--stdout] [--file out.raw] [--no-alsa]\n", argv0);
}

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    if (argc < 3) { usage(argv[0]); return 1; }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *outfile = NULL;
    int no_alsa = 0;
    int to_stdout = 0;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) outfile = argv[++i];
        else if (strcmp(argv[i], "--no-alsa") == 0) no_alsa = 1;
        else if (strcmp(argv[i], "--stdout") == 0) { to_stdout = 1; no_alsa = 1; }
        else { usage(argv[0]); return 1; }
    }

    int sock = connect_to_server(host, port);
    if (sock < 0) return 1;
    int outfd = -1;
    if (outfile) {
        outfd = open(outfile, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (outfd < 0) { perror(outfile); close(sock); return 1; }
    }

#ifdef HAVE_ALSA
    snd_pcm_t *pcm = NULL;
    if (!no_alsa) pcm = open_alsa();
    if (!pcm && !outfile && !to_stdout) fprintf(stderr, "ALSA unavailable. Use --stdout or --file out.raw.\n");
#else
    (void)no_alsa;
    if (!outfile && !to_stdout) fprintf(stderr, "built without ALSA. Use --stdout or --file out.raw.\n");
#endif

    fprintf(stderr, "connected to %s:%d; receiving PCM S16_LE mono %d Hz\n", host, port, BB_SAMPLE_RATE);
    fprintf(stderr, "Ctrl+C to stop. %s%s%s\n", outfile ? "also writing raw file. " : "", to_stdout ? "streaming PCM to stdout. " : "", no_alsa ? "ALSA disabled." : "");

    int16_t block[BB_BLOCK_FRAMES];
    while (g_running) {
        int r = bb_read_all(sock, block, sizeof(block));
        if (r <= 0) break;
        if (outfd >= 0 && bb_write_all(outfd, block, sizeof(block)) < 0) { perror("write output"); break; }
        if (to_stdout && bb_write_all(STDOUT_FILENO, block, sizeof(block)) < 0) break;
#ifdef HAVE_ALSA
        if (pcm) play_alsa(pcm, block, BB_BLOCK_FRAMES);
#endif
    }

#ifdef HAVE_ALSA
    if (pcm) { snd_pcm_drain(pcm); snd_pcm_close(pcm); }
#endif
    if (outfd >= 0) close(outfd);
    close(sock);
    return 0;
}
