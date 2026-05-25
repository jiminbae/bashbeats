#include "file_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

/* ── file_ensure_saves_dir ── */
int file_ensure_saves_dir(void)
{
    struct stat st;
    if (stat(SAVES_DIR, &st) == 0) return 0;     /* already exists */
    if (mkdir(SAVES_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[file_io] Cannot create %s: %s\n",
                SAVES_DIR, strerror(errno));
        return -1;
    }
    return 0;
}

/* ── file_list_instruments ──
 * Scans SAMPLES_DIR for *.wav files.
 * Returns count; fills out[] with relative paths "samples/foo.wav". */
int file_list_instruments(char out[][128], int max)
{
    DIR *dp = opendir(SAMPLES_DIR);
    if (!dp) return 0;

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && n < max) {
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len > 4 && strcmp(name + len - 4, ".wav") == 0) {
            snprintf(out[n], 128, "%s/%s", SAMPLES_DIR, name);
            n++;
        }
    }
    closedir(dp);
    return n;
}

/* ── file_ensure_stub_instrument ──
 * Writes a minimal valid 44-byte WAV (0 samples / pure silence) to
 * samples/silent.wav if it does not already exist. */
void file_ensure_stub_instrument(void)
{
    /* Ensure samples/ directory exists */
    struct stat st;
    if (stat(SAMPLES_DIR, &st) != 0)
        mkdir(SAMPLES_DIR, 0755);

    char path[64];
    snprintf(path, sizeof(path), "%s/silent.wav", SAMPLES_DIR);
    if (stat(path, &st) == 0) return;   /* already exists */

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[file_io] Cannot create stub WAV: %s\n", path);
        return;
    }

    /* Minimal PCM WAV header (44 bytes, 0 data bytes) */
    uint32_t data_size   = 0;
    uint32_t chunk_size  = 36 + data_size;
    uint16_t audio_fmt   = 1;       /* PCM */
    uint16_t num_ch      = 1;
    uint32_t sample_rate = 44100;
    uint16_t bits        = 16;
    uint16_t block_align = num_ch * (bits / 8);
    uint32_t byte_rate   = sample_rate * block_align;
    uint16_t sub2_size   = 16;

    /* RIFF chunk */
    fwrite("RIFF", 1, 4, fp);
    fwrite(&chunk_size,  4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    /* fmt sub-chunk */
    fwrite("fmt ", 1, 4, fp);
    fwrite(&sub2_size,   4, 1, fp);
    fwrite(&audio_fmt,   2, 1, fp);
    fwrite(&num_ch,      2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate,   4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits,        2, 1, fp);
    /* data sub-chunk */
    fwrite("data", 1, 4, fp);
    fwrite(&data_size,   4, 1, fp);

    fclose(fp);
    fprintf(stderr, "[file_io] Created stub instrument: %s\n", path);
}

/* ── file_default_path ── */
void file_default_path(const char *title, char *out, int len)
{
    /* Sanitise title: replace spaces and slashes with '_' */
    char safe[64];
    strncpy(safe, title, 63);
    safe[63] = '\0';
    for (int i = 0; safe[i]; i++) {
        if (safe[i] == ' ' || safe[i] == '/' || safe[i] == '\\')
            safe[i] = '_';
    }
    snprintf(out, len, "%s/%s.bbeat", SAVES_DIR, safe);
}

/* ── project_new ── */
Project *project_new(const char *instrument_path)
{
    Project *p = calloc(1, sizeof(Project));
    if (!p) return NULL;

    snprintf(p->title, sizeof(p->title), "Untitled");
    p->bpm         = 120;
    p->track_count = 1;

    Track *t = &p->tracks[0];
    snprintf(t->name, sizeof(t->name), "Track 1");
    t->volume      = 1.0f;
    t->mute        = 0;
    t->event_count = 0;
    t->base_note   = 60;   /* middle C */

    if (instrument_path && instrument_path[0]) {
        strncpy(t->instrument, instrument_path, 127);
        t->instrument[127] = '\0';
    } else {
        snprintf(t->instrument, 128, "%s/piano.wav", SAMPLES_DIR);
    }

    return p;
}

/* ── project_load ── */
Project *project_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "[file_io] Cannot open: %s\n", path);
        return NULL;
    }

    Project *p = calloc(1, sizeof(Project));
    if (!p) { fclose(fp); return NULL; }
    p->bpm = 120;
    snprintf(p->title, sizeof(p->title), "Untitled");

    char line[512];
    int  cur = -1;   /* current track index */

    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (len == 0 || line[0] == '#') continue;

        char *tok = line;
        while (*tok == ' ' || *tok == '\t') tok++;

        if (strncmp(tok, "TITLE ", 6) == 0) {
            strncpy(p->title, tok + 6, sizeof(p->title) - 1);
            p->title[sizeof(p->title) - 1] = '\0';

        } else if (strncmp(tok, "BPM ", 4) == 0) {
            p->bpm = atoi(tok + 4);
            if (p->bpm < 20 || p->bpm > 300) p->bpm = 120;

        } else if (strncmp(tok, "TRACK ", 6) == 0) {
            int idx = atoi(tok + 6);
            if (idx < 0 || idx >= MAX_TRACKS) continue;
            cur = idx;
            if (idx >= p->track_count) p->track_count = idx + 1;
            /* defaults */
            p->tracks[idx].volume    = 1.0f;
            p->tracks[idx].mute      = 0;
            p->tracks[idx].base_note = 60;
            p->tracks[idx].event_count = 0;
            snprintf(p->tracks[idx].instrument, 128, "%s/piano.wav", SAMPLES_DIR);

        } else if (cur >= 0 && strncmp(tok, "NAME ", 5) == 0) {
            strncpy(p->tracks[cur].name, tok + 5, 31);
            p->tracks[cur].name[31] = '\0';

        } else if (cur >= 0 && strncmp(tok, "INSTR ", 6) == 0) {
            strncpy(p->tracks[cur].instrument, tok + 6, 127);
            p->tracks[cur].instrument[127] = '\0';

        } else if (cur >= 0 && strncmp(tok, "BASE ", 5) == 0) {
            int bn = atoi(tok + 5);
            p->tracks[cur].base_note = (bn >= 12 && bn <= 115) ? bn : 60;

        } else if (cur >= 0 && strncmp(tok, "VOL ", 4) == 0) {
            float v = (float)atof(tok + 4);
            p->tracks[cur].volume = (v >= 0.0f && v <= 1.0f) ? v : 1.0f;

        } else if (cur >= 0 && strncmp(tok, "MUTE ", 5) == 0) {
            p->tracks[cur].mute = atoi(tok + 5) ? 1 : 0;

        } else if (strcmp(tok, "END") == 0) {
            cur = -1;

        } else if (cur >= 0) {
            /* Note line: start_tick note duration_tick velocity */
            Track *t = &p->tracks[cur];
            if (t->event_count >= MAX_EVENTS) continue;
            NoteEvent *ev = &t->events[t->event_count];
            unsigned st = 0, nt = 0, dt = 0, vel = 80;
            int parsed = sscanf(tok, "%u %u %u %u", &st, &nt, &dt, &vel);
            if (parsed < 3) continue;
            ev->start_tick    = st;
            ev->note          = (uint8_t)(nt > 127 ? 127 : nt);
            ev->duration_tick = dt > 0 ? dt : 1;
            ev->velocity      = (uint8_t)(vel > 127 ? 127 : vel);
            t->event_count++;
        }
    }

    fclose(fp);
    return p;
}

/* ── project_save ── */
int project_save(const Project *p, const char *path)
{
    if (!p || !path) return -1;

    /* Ensure saves/ dir exists */
    file_ensure_saves_dir();

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "[file_io] Cannot write: %s\n", path);
        return -1;
    }

    fprintf(fp, "# BashBeats project file\n");
    fprintf(fp, "TITLE %s\n", p->title);
    fprintf(fp, "BPM %d\n",   p->bpm);

    for (int i = 0; i < p->track_count; i++) {
        const Track *t = &p->tracks[i];
        fprintf(fp, "\nTRACK %d\n",      i);
        fprintf(fp, "  NAME  %s\n",      t->name);
        fprintf(fp, "  INSTR %s\n",      t->instrument);
        fprintf(fp, "  BASE  %d\n",      t->base_note);
        fprintf(fp, "  VOL   %.2f\n",    t->volume);
        fprintf(fp, "  MUTE  %d\n",      t->mute);
        fprintf(fp, "  # start_tick  note  duration_tick  velocity\n");
        for (int j = 0; j < t->event_count; j++) {
            const NoteEvent *ev = &t->events[j];
            fprintf(fp, "  %u  %u  %u  %u\n",
                    ev->start_tick,
                    (unsigned)ev->note,
                    ev->duration_tick,
                    (unsigned)ev->velocity);
        }
        fprintf(fp, "END\n");
    }

    fclose(fp);
    return 0;
}

/* ── project_free ── */
void project_free(Project *p)
{
    free(p);
}
