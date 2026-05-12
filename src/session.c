#include "session.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const float note_pitch[BB_PIANO_NOTES] = {
    1.0000f, 1.12246f, 1.25992f, 1.33484f, 1.49831f, 1.68179f, 1.88775f
};

void session_init(Session *s) {
    memset(s, 0, sizeof(*s));
    s->bpm = 120;
    for (int t = 0; t < BB_TRACKS; ++t) {
        s->volume[t] = 0.75f;
        s->pitch[t] = 1.0f;
    }
    s->volume[0] = 0.90f; s->volume[1] = 0.65f; s->volume[2] = 0.45f;
    for (int n = 0; n < BB_PIANO_NOTES; ++n) {
        int t = BB_DRUM_TRACKS + n;
        s->volume[t] = 0.32f;
        s->pitch[t] = note_pitch[n];
    }

    int kick[]  = {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0};
    int snare[] = {0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0};
    int hat[]   = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};
    memcpy(s->pattern[0], kick, sizeof(kick));
    memcpy(s->pattern[1], snare, sizeof(snare));
    memcpy(s->pattern[2], hat, sizeof(hat));
    /* A tiny C-major phrase across the one-octave piano roll. */
    s->pattern[3][0] = 1;  s->pattern[4][2] = 1;  s->pattern[5][4] = 1;  s->pattern[7][6] = 1;
    s->pattern[8][8] = 1;  s->pattern[7][10] = 1; s->pattern[5][12] = 1; s->pattern[4][14] = 1;
}

int session_save(const char *path, const Session *s) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) { perror(path); return -1; }
    char buf[8192];
    int n = snprintf(buf, sizeof(buf), "BASHBEATS 2\nBPM %d\n", s->bpm);
    for (int t = 0; t < BB_TRACKS; ++t) {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "TRACK %d %.3f %.3f\n", t, s->volume[t], s->pitch[t]);
        for (int i = 0; i < BB_STEPS && n < (int)sizeof(buf)-2; ++i) buf[n++] = s->pattern[t][i] ? '1' : '0';
        buf[n++] = '\n';
    }
    int ok = bb_write_all(fd, buf, (size_t)n);
    close(fd);
    return ok;
}

int session_load(const char *path, Session *s) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;

    Session tmp;
    session_init(&tmp);
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    if (!line || (strcmp(line, "BASHBEATS 1") != 0 && strcmp(line, "BASHBEATS 2") != 0)) return -1;
    while ((line = strtok_r(NULL, "\n", &save))) {
        if (!strncmp(line, "BPM ", 4)) {
            sscanf(line + 4, "%d", &tmp.bpm);
        } else if (!strncmp(line, "TRACK ", 6)) {
            int t = 0;
            float vol = 1.0f, pitch = 1.0f;
            sscanf(line, "TRACK %d %f %f", &t, &vol, &pitch);
            char *pat = strtok_r(NULL, "\n", &save);
            if (t >= 0 && t < BB_TRACKS && pat) {
                tmp.volume[t] = vol;
                tmp.pitch[t] = pitch;
                for (int i = 0; i < BB_STEPS && pat[i]; ++i) tmp.pattern[t][i] = pat[i] == '1';
            }
        }
    }
    tmp.bpm = BB_CLAMP(tmp.bpm, 40, 240);
    *s = tmp;
    return 0;
}
