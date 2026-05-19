#include "data.h"
#include "audio.h"
#include "editor.h"
#include "file_io.h"
#include "stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STREAM_PORT 9000

static Project *g_project = NULL;

static void cleanup(void)
{
    editor_cleanup();
    audio_cleanup();
    stream_cleanup();
    project_free(g_project);
    g_project = NULL;
}

int main(int argc, char *argv[])
{
    /* Ensure runtime directories exist */
    file_ensure_saves_dir();
    file_ensure_stub_instrument();  /* creates samples/silent.wav if absent */

    if (argc >= 2) {
        g_project = project_load(argv[1]);
        if (!g_project) {
            fprintf(stderr, "Failed to load: %s\n", argv[1]);
            return 1;
        }
    } else {
        /* New project: use stub silent instrument */
        char stub[128];
        snprintf(stub, sizeof(stub), "%s/silent.wav", SAMPLES_DIR);
        g_project = project_new(stub);
        if (!g_project) {
            fprintf(stderr, "Failed to create project.\n");
            return 1;
        }
    }

    atexit(cleanup);

    if (audio_init(SAMPLES_DIR) != 0)
        fprintf(stderr, "audio_init failed — continuing with stub.\n");

    audio_set_bpm(g_project->bpm);

    /* Notify audio engine of all track instruments */
    for (int i = 0; i < g_project->track_count; i++)
        audio_load_instrument(i, g_project->tracks[i].instrument);

    if (stream_init(STREAM_PORT) != 0)
        fprintf(stderr, "stream_init failed — streaming disabled.\n");

    editor_init(g_project);
    editor_run(g_project);

    return 0;
}
