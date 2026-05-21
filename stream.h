#ifndef BASHBEATS_STREAM_H
#define BASHBEATS_STREAM_H

#include <stdint.h>

/* ── TCP PCM Streaming Server ──
 * Implemented by the editor/comms part (Part B).
 * The audio thread calls stream_send() after each render chunk.
 * All connected clients receive the raw PCM broadcast.
 *
 * Client connection (same network):
 *   nc <server_ip> 9000 | aplay -f S16_LE -r 44100 -c 2
 */

int  stream_init   (int port);              /* open server socket; 0=ok, -1=fail */
void stream_send   (const int16_t *buf, int frames); /* broadcast PCM to all clients */
int  stream_clients(void);                  /* current connected client count */
void stream_cleanup(void);

#endif /* BASHBEATS_STREAM_H */
