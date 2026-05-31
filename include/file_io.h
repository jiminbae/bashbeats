#ifndef BASHBEATS_FILE_IO_H
#define BASHBEATS_FILE_IO_H

#include "data.h"

/* ── .bbeat file format ──────────────────────────────────────────────
 *
 * Saved under SAVES_DIR (default: "saves/").
 * Format (text, UTF-8):
 *
 *   TITLE MyProject
 *   BPM 120
 *
 *   TRACK 0
 *     NAME    melody
 *     INSTR   samples/piano.wav
 *     BASE    60
 *     VOL     1.00
 *     MUTE    0
 *     # start_tick  note  duration_tick  velocity
 *     0    60  4   80
 *   END
 *
 * ─────────────────────────────────────────────────────────────────── */

/* Ensure SAVES_DIR exists (creates it if absent). Returns 0 on success. */
int  file_ensure_saves_dir(void);

/* Scan SAMPLES_DIR for *.wav files.
 * Fills out[0..max-1] with relative paths like "samples/piano.wav".
 * Returns number of files found (0 if none or dir absent). */
int  file_list_instruments(char out[][264], int max);

/* Ensure a fallback silent.wav exists for empty/missing sample sets. */
void file_ensure_stub_instrument(void);

/* Build the default save path: "saves/<title>.bbeat" into out (len bytes). */
void file_default_path(const char *title, char *out, int len);

Project *project_new (const char *instrument_path); /* NULL -> use samples/piano.wav */
Project *project_load(const char *path);
int      project_save(const Project *p, const char *path);
void     project_free(Project *p);

#endif /* BASHBEATS_FILE_IO_H */
