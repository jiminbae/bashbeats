#include "stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAX_CLIENTS 8

/* ── Internal state ── */
static int s_server_fd              = -1;
static int s_client_fds[MAX_CLIENTS];
static int s_client_count           = 0;
static pthread_mutex_t s_stream_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t       s_accept_tid;
static volatile int    s_running    = 0;

/* ── Accept loop (background thread) ── */
static void *accept_loop(void *arg)
{
    (void)arg;
    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int cfd = accept(s_server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (cfd < 0) {
            if (s_running) perror("[stream] accept");
            continue;
        }

        pthread_mutex_lock(&s_stream_mtx);
        if (s_client_count < MAX_CLIENTS) {
            s_client_fds[s_client_count++] = cfd;
            fprintf(stderr, "[stream] client connected: %s (total=%d)\n",
                    inet_ntoa(client_addr.sin_addr), s_client_count);
        } else {
            fprintf(stderr, "[stream] client rejected: max clients reached\n");
            close(cfd);
        }
        pthread_mutex_unlock(&s_stream_mtx);
    }
    return NULL;
}

/* ── stream_init ── */
int stream_init(int port)
{
    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        perror("[stream] socket");
        return -1;
    }

    /* Allow immediate rebind after restart */
    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[stream] bind");
        close(s_server_fd);
        s_server_fd = -1;
        return -1;
    }

    if (listen(s_server_fd, 4) < 0) {
        perror("[stream] listen");
        close(s_server_fd);
        s_server_fd = -1;
        return -1;
    }

    memset(s_client_fds, -1, sizeof(s_client_fds));
    s_client_count = 0;
    s_running      = 1;

    if (pthread_create(&s_accept_tid, NULL, accept_loop, NULL) != 0) {
        perror("[stream] pthread_create");
        close(s_server_fd);
        s_server_fd = -1;
        return -1;
    }

    fprintf(stderr, "[stream] listening on port %d\n", port);
    return 0;
}

/* ── stream_send ──
 * Broadcasts raw PCM (int16_t stereo interleaved) to all clients.
 * Clients that have disconnected are removed from the list. */
void stream_send(const int16_t *buf, int frames)
{
    if (s_client_count == 0 || !buf || frames <= 0) return;

    int   nbytes = frames * 2 * sizeof(int16_t); /* stereo */
    pthread_mutex_lock(&s_stream_mtx);

    int i = 0;
    while (i < s_client_count) {
        ssize_t written = write(s_client_fds[i], buf, nbytes);
        if (written < 0) {
            /* Client disconnected — remove from list */
            fprintf(stderr, "[stream] client disconnected (fd=%d)\n", s_client_fds[i]);
            close(s_client_fds[i]);
            /* Shift remaining clients down */
            s_client_fds[i] = s_client_fds[--s_client_count];
        } else {
            i++;
        }
    }

    pthread_mutex_unlock(&s_stream_mtx);
}

/* ── stream_clients ── */
int stream_clients(void)
{
    pthread_mutex_lock(&s_stream_mtx);
    int n = s_client_count;
    pthread_mutex_unlock(&s_stream_mtx);
    return n;
}

/* ── stream_cleanup ── */
void stream_cleanup(void)
{
    s_running = 0;

    if (s_server_fd >= 0) {
        shutdown(s_server_fd, SHUT_RDWR);
        close(s_server_fd);
        s_server_fd = -1;
    }

    pthread_join(s_accept_tid, NULL);

    pthread_mutex_lock(&s_stream_mtx);
    for (int i = 0; i < s_client_count; i++) {
        if (s_client_fds[i] >= 0) close(s_client_fds[i]);
    }
    s_client_count = 0;
    pthread_mutex_unlock(&s_stream_mtx);

    fprintf(stderr, "[stream] cleanup done\n");
}
