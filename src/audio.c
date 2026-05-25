#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "audio.h"
#include "stream.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_CHANNELS   2
#define AUDIO_BLOCK      256
#define MAX_VOICES       128
#define MASTER_GAIN      0.58
#define RELEASE_FRAMES   384

typedef struct {
    int sample_rate;
    size_t frames;
    int16_t *pcm_mono;
} WavSample;

typedef struct {
    WavSample sample;
    char path[128];
    int loaded;
} InstrumentSlot;

typedef struct {
    int active;
    int track;
    int note;
    double pos;
    double inc;
    float gain;
    int64_t frames_left;
    int release_left;
} Voice;

static InstrumentSlot s_slots[MAX_TRACKS];
static Voice s_voices[MAX_VOICES];
static pthread_mutex_t s_audio_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_thread;
static int s_thread_started = 0;
static volatile int s_running = 0;

static Project *s_project = NULL;
static int s_playing = 0;
static int s_paused = 0;
static int s_bpm = 120;
static uint32_t s_tick = 0;         /* next tick to render */
static uint32_t s_display_tick = 0; /* last tick rendered, used by the UI */
static double s_frames_to_next_tick = 0.0;
static FILE *s_aplay = NULL;

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16_le(FILE *fp, uint16_t v)
{
    unsigned char b[2] = { (unsigned char)(v & 0xff), (unsigned char)(v >> 8) };
    fwrite(b, 1, sizeof(b), fp);
}

static void write_u32_le(FILE *fp, uint32_t v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 0xff),
        (unsigned char)((v >> 8) & 0xff),
        (unsigned char)((v >> 16) & 0xff),
        (unsigned char)((v >> 24) & 0xff)
    };
    fwrite(b, 1, sizeof(b), fp);
}

static int16_t mix_to_i16(int mix)
{
    double x = ((double)mix / 32768.0) * MASTER_GAIN;
    double y = tanh(1.25 * x) / tanh(1.25);
    if (y > 0.98) y = 0.98;
    if (y < -0.98) y = -0.98;
    return (int16_t)(y * 32767.0);
}

static double frames_per_tick(int bpm)
{
    if (bpm < 20) bpm = 20;
    if (bpm > 300) bpm = 300;
    return ((60.0 / (double)bpm) / (double)TICKS_PER_QN) * AUDIO_SAMPLE_RATE;
}

static void wav_free(WavSample *s)
{
    if (!s) return;
    free(s->pcm_mono);
    memset(s, 0, sizeof(*s));
}

static int wav_load_mono16(const char *path, WavSample *out)
{
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[audio] cannot open wav: %s (%s)\n", path, strerror(errno));
        return -1;
    }

    unsigned char riff[12];
    if (fread(riff, 1, sizeof(riff), fp) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "[audio] invalid wav: %s\n", path);
        fclose(fp);
        return -1;
    }

    int have_fmt = 0, have_data = 0;
    uint16_t audio_format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0, data_size = 0;
    long data_pos = 0;

    for (;;) {
        unsigned char chdr[8];
        if (fread(chdr, 1, sizeof(chdr), fp) != sizeof(chdr)) break;
        uint32_t size = read_u32_le(chdr + 4);

        if (memcmp(chdr, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), fp) != sizeof(fmt)) {
                fclose(fp);
                return -1;
            }
            audio_format = read_u16_le(fmt);
            channels = read_u16_le(fmt + 2);
            sample_rate = read_u32_le(fmt + 4);
            bits = read_u16_le(fmt + 14);
            if (size > sizeof(fmt)) fseek(fp, (long)(size - sizeof(fmt)), SEEK_CUR);
            have_fmt = 1;
        } else if (memcmp(chdr, "data", 4) == 0) {
            data_pos = ftell(fp);
            data_size = size;
            fseek(fp, (long)size, SEEK_CUR);
            have_data = 1;
        } else {
            fseek(fp, (long)size, SEEK_CUR);
        }
        if (size & 1) fseek(fp, 1, SEEK_CUR);
    }

    if (!have_fmt || !have_data || audio_format != 1 || bits != 16 || channels < 1) {
        fprintf(stderr, "[audio] unsupported wav, need PCM 16-bit: %s\n", path);
        fclose(fp);
        return -1;
    }

    size_t total_samples = data_size / sizeof(int16_t);
    size_t frames = total_samples / channels;
    int16_t *raw = malloc(total_samples * sizeof(int16_t));
    int16_t *mono = calloc(frames ? frames : 1, sizeof(int16_t));
    if (!raw || !mono) {
        free(raw);
        free(mono);
        fclose(fp);
        return -1;
    }

    fseek(fp, data_pos, SEEK_SET);
    if (fread(raw, sizeof(int16_t), total_samples, fp) != total_samples) {
        free(raw);
        free(mono);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    for (size_t f = 0; f < frames; f++) {
        int sum = 0;
        for (uint16_t c = 0; c < channels; c++) {
            sum += raw[f * channels + c];
        }
        mono[f] = (int16_t)(sum / (int)channels);
    }

    free(raw);
    out->sample_rate = (int)sample_rate;
    out->frames = frames;
    out->pcm_mono = mono;
    return 0;
}

static int16_t sample_at(const WavSample *s, double pos)
{
    if (!s || !s->pcm_mono || s->frames < 2 || pos < 0.0 || pos >= (double)(s->frames - 1)) {
        return 0;
    }
    size_t i = (size_t)pos;
    double frac = pos - (double)i;
    double a = s->pcm_mono[i];
    double b = s->pcm_mono[i + 1];
    return (int16_t)(a + (b - a) * frac);
}

static void trigger_voice_locked(int track, int note, float gain, uint32_t duration_ticks)
{
    if (track < 0 || track >= MAX_TRACKS || !s_slots[track].loaded ||
        !s_slots[track].sample.pcm_mono || s_slots[track].sample.frames < 2) {
        return;
    }

    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!s_voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;

    int base_note = 60;
    if (s_project && track < s_project->track_count && s_project->tracks[track].base_note >= 0) {
        base_note = s_project->tracks[track].base_note;
    }

    double semitone = (double)(note - base_note);
    double pitch = pow(2.0, semitone / 12.0);
    double resample = (double)s_slots[track].sample.sample_rate / (double)AUDIO_SAMPLE_RATE;
    int64_t frames_left = -1;
    if (duration_ticks > 1) {
        frames_left = (int64_t)(duration_ticks * frames_per_tick(s_bpm));
    }

    s_voices[slot] = (Voice){
        .active = 1,
        .track = track,
        .note = note,
        .pos = 0.0,
        .inc = resample * pitch,
        .gain = gain,
        .frames_left = frames_left,
        .release_left = 0
    };
}

static void begin_release(Voice *voice)
{
    if (!voice || !voice->active) return;
    if (voice->release_left <= 0) voice->release_left = RELEASE_FRAMES;
    voice->frames_left = -1;
}

static float voice_release_gain(Voice *voice)
{
    if (!voice || voice->release_left <= 0) return 1.0f;
    return (float)voice->release_left / (float)RELEASE_FRAMES;
}

static void advance_voice_release(Voice *voice)
{
    if (!voice || voice->release_left <= 0) return;
    voice->release_left--;
    if (voice->release_left <= 0) voice->active = 0;
}

static void fire_project_tick_locked(uint32_t tick)
{
    if (!s_project) return;

    pthread_mutex_lock(&g_project_mtx);
    int tracks = s_project->track_count;
    if (tracks > MAX_TRACKS) tracks = MAX_TRACKS;

    for (int t = 0; t < tracks; t++) {
        Track *tr = &s_project->tracks[t];
        if (tr->mute) continue;
        for (int e = 0; e < tr->event_count; e++) {
            NoteEvent *ev = &tr->events[e];
            if (ev->start_tick == tick) {
                float gain = tr->volume * ((float)ev->velocity / 127.0f);
                trigger_voice_locked(t, ev->note, gain, ev->duration_tick);
            }
        }
    }
    pthread_mutex_unlock(&g_project_mtx);
}

static void render_live_block(int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * AUDIO_CHANNELS * sizeof(int16_t));

    pthread_mutex_lock(&s_audio_mtx);
    for (int f = 0; f < frames; f++) {
        if (s_playing && !s_paused && s_project) {
            if (s_frames_to_next_tick <= 0.0) {
                fire_project_tick_locked(s_tick);
                s_display_tick = s_tick;
                s_tick++;
                s_frames_to_next_tick += frames_per_tick(s_bpm);
            }
            s_frames_to_next_tick -= 1.0;
        }

        int mix = 0;
        for (int v = 0; v < MAX_VOICES; v++) {
            Voice *voice = &s_voices[v];
            if (!voice->active) continue;

            WavSample *sample = &s_slots[voice->track].sample;
            if (voice->pos >= (double)(sample->frames - 1)) {
                voice->active = 0;
                continue;
            }
            if (voice->frames_left == 0) begin_release(voice);

            mix += (int)((float)sample_at(sample, voice->pos) *
                         voice->gain * voice_release_gain(voice));
            voice->pos += voice->inc;
            if (voice->frames_left > 0) voice->frames_left--;
            advance_voice_release(voice);
        }

        int16_t s = mix_to_i16(mix);
        out[f * 2] = s;
        out[f * 2 + 1] = s;
    }
    pthread_mutex_unlock(&s_audio_mtx);
}

static void close_aplay(void)
{
    if (!s_aplay) return;
    pclose(s_aplay);
    s_aplay = NULL;
}

static void open_aplay(void)
{
    if (s_aplay) return;
    s_aplay = popen("aplay -q -f S16_LE -c 2 -r 44100 -B 20000 -F 5000 2>/tmp/bashbeats_aplay.err", "w");
    if (!s_aplay) {
        fprintf(stderr, "[audio] aplay unavailable; TCP streaming/export still work\n");
    } else {
        setvbuf(s_aplay, NULL, _IONBF, 0);
    }
}

static void *audio_thread(void *arg)
{
    (void)arg;
    int16_t block[AUDIO_BLOCK * AUDIO_CHANNELS];
    const long ns = (long)((1000000000.0 * AUDIO_BLOCK) / AUDIO_SAMPLE_RATE);

    while (s_running) {
        render_live_block(block, AUDIO_BLOCK);

        if (s_aplay) {
            size_t wrote = fwrite(block, sizeof(int16_t), AUDIO_BLOCK * AUDIO_CHANNELS, s_aplay);
            if (wrote != AUDIO_BLOCK * AUDIO_CHANNELS || ferror(s_aplay)) {
                clearerr(s_aplay);
                close_aplay();
            } else {
                fflush(s_aplay);
            }
        }

        stream_send(block, AUDIO_BLOCK);

        struct timespec ts = { ns / 1000000000L, ns % 1000000000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

int audio_init(const char *sample_dir)
{
    (void)sample_dir;
    signal(SIGPIPE, SIG_IGN);

    pthread_mutex_lock(&s_audio_mtx);
    if (s_thread_started) {
        pthread_mutex_unlock(&s_audio_mtx);
        return 0;
    }
    s_running = 1;
    s_bpm = 120;
    s_tick = 0;
    s_display_tick = 0;
    s_frames_to_next_tick = 0.0;
    memset(s_voices, 0, sizeof(s_voices));
    pthread_mutex_unlock(&s_audio_mtx);

    open_aplay();
    if (pthread_create(&s_thread, NULL, audio_thread, NULL) != 0) {
        s_running = 0;
        close_aplay();
        return -1;
    }
    s_thread_started = 1;
    fprintf(stderr, "[audio] engine ready (44100 Hz stereo)\n");
    return 0;
}

void audio_cleanup(void)
{
    if (s_thread_started) {
        s_running = 0;
        pthread_join(s_thread, NULL);
        s_thread_started = 0;
    }

    pthread_mutex_lock(&s_audio_mtx);
    s_playing = 0;
    s_paused = 0;
    s_project = NULL;
    memset(s_voices, 0, sizeof(s_voices));
    for (int i = 0; i < MAX_TRACKS; i++) {
        wav_free(&s_slots[i].sample);
        s_slots[i].loaded = 0;
        s_slots[i].path[0] = '\0';
    }
    pthread_mutex_unlock(&s_audio_mtx);

    close_aplay();
    fprintf(stderr, "[audio] cleanup done\n");
}

void audio_load_instrument(int track_idx, const char *wav_path)
{
    if (track_idx < 0 || track_idx >= MAX_TRACKS || !wav_path || !wav_path[0]) return;

    WavSample sample;
    int ok = wav_load_mono16(wav_path, &sample);

    pthread_mutex_lock(&s_audio_mtx);
    wav_free(&s_slots[track_idx].sample);
    s_slots[track_idx].loaded = 0;
    strncpy(s_slots[track_idx].path, wav_path, sizeof(s_slots[track_idx].path) - 1);
    s_slots[track_idx].path[sizeof(s_slots[track_idx].path) - 1] = '\0';

    if (ok == 0) {
        s_slots[track_idx].sample = sample;
        s_slots[track_idx].loaded = 1;
        fprintf(stderr, "[audio] loaded track %d: %s\n", track_idx, wav_path);
    } else {
        fprintf(stderr, "[audio] failed to load track %d: %s\n", track_idx, wav_path);
    }
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_note_on(int track_idx, int note, float vol)
{
    if (note < 0 || note > 127) return;
    if (track_idx < 0 || track_idx >= MAX_TRACKS) track_idx = 0;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.5f) vol = 1.5f;

    pthread_mutex_lock(&s_audio_mtx);
    trigger_voice_locked(track_idx, note, vol, 0);
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_note_off(int track_idx, int note)
{
    pthread_mutex_lock(&s_audio_mtx);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (s_voices[i].active &&
            (track_idx < 0 || s_voices[i].track == track_idx) &&
            s_voices[i].note == note) {
            begin_release(&s_voices[i]);
        }
    }
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_play(Project *p)
{
    if (!p) return;
    pthread_mutex_lock(&s_audio_mtx);
    s_project = p;
    s_bpm = p->bpm;
    s_playing = 1;
    s_paused = 0;
    s_frames_to_next_tick = 0.0;
    s_display_tick = s_tick;
    memset(s_voices, 0, sizeof(s_voices));
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_pause(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    s_paused = 1;
    memset(s_voices, 0, sizeof(s_voices));
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_resume(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    s_paused = 0;
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_stop(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    s_playing = 0;
    s_paused = 0;
    s_project = NULL;
    memset(s_voices, 0, sizeof(s_voices));
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_set_bpm(int bpm)
{
    if (bpm < 20) bpm = 20;
    if (bpm > 300) bpm = 300;
    pthread_mutex_lock(&s_audio_mtx);
    s_bpm = bpm;
    pthread_mutex_unlock(&s_audio_mtx);
}

void audio_seek_tick(uint32_t tick)
{
    pthread_mutex_lock(&s_audio_mtx);
    s_tick = tick;
    s_display_tick = tick;
    s_frames_to_next_tick = 0.0;
    memset(s_voices, 0, sizeof(s_voices));
    pthread_mutex_unlock(&s_audio_mtx);
}

int audio_is_playing(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    int v = s_playing && !s_paused;
    pthread_mutex_unlock(&s_audio_mtx);
    return v;
}

int audio_is_paused(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    int v = s_paused;
    pthread_mutex_unlock(&s_audio_mtx);
    return v;
}

uint32_t audio_current_tick(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    uint32_t v = s_display_tick;
    pthread_mutex_unlock(&s_audio_mtx);
    return v;
}

int audio_get_bpm(void)
{
    pthread_mutex_lock(&s_audio_mtx);
    int v = s_bpm;
    pthread_mutex_unlock(&s_audio_mtx);
    return v;
}

static uint32_t project_last_tick(const Project *p)
{
    uint32_t last = 16;
    if (!p) return last;
    for (int t = 0; t < p->track_count; t++) {
        for (int e = 0; e < p->tracks[t].event_count; e++) {
            uint32_t end = p->tracks[t].events[e].start_tick +
                           p->tracks[t].events[e].duration_tick;
            if (end > last) last = end;
        }
    }
    return last;
}

static void offline_trigger(Voice *voices, WavSample *samples, const Project *p,
                            int track, int note, float gain, uint32_t duration_ticks)
{
    if (track < 0 || track >= MAX_TRACKS || !samples[track].pcm_mono ||
        samples[track].frames < 2) {
        return;
    }

    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;

    int base_note = 60;
    if (track < p->track_count && p->tracks[track].base_note >= 0) {
        base_note = p->tracks[track].base_note;
    }
    double pitch = pow(2.0, (double)(note - base_note) / 12.0);
    double resample = (double)samples[track].sample_rate / (double)AUDIO_SAMPLE_RATE;
    int64_t frames_left = -1;
    if (duration_ticks > 1) {
        frames_left = (int64_t)(duration_ticks * frames_per_tick(p->bpm));
    }

    voices[slot] = (Voice){
        .active = 1,
        .track = track,
        .note = note,
        .pos = 0.0,
        .inc = resample * pitch,
        .gain = gain,
        .frames_left = frames_left,
        .release_left = 0
    };
}

int audio_export_wav(Project *p, const char *path)
{
    if (!p || !path || !path[0]) return -1;

    WavSample samples[MAX_TRACKS];
    memset(samples, 0, sizeof(samples));
    for (int t = 0; t < p->track_count && t < MAX_TRACKS; t++) {
        wav_load_mono16(p->tracks[t].instrument, &samples[t]);
    }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        for (int t = 0; t < MAX_TRACKS; t++) wav_free(&samples[t]);
        return -1;
    }

    uint32_t last_tick = project_last_tick(p) + (TICKS_PER_QN * 2);
    uint32_t frames = (uint32_t)((double)last_tick * frames_per_tick(p->bpm));
    uint32_t data_bytes = frames * AUDIO_CHANNELS * sizeof(int16_t);

    fwrite("RIFF", 1, 4, fp);
    write_u32_le(fp, 36 + data_bytes);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    write_u32_le(fp, 16);
    write_u16_le(fp, 1);
    write_u16_le(fp, AUDIO_CHANNELS);
    write_u32_le(fp, AUDIO_SAMPLE_RATE);
    write_u32_le(fp, AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * sizeof(int16_t));
    write_u16_le(fp, AUDIO_CHANNELS * sizeof(int16_t));
    write_u16_le(fp, 16);
    fwrite("data", 1, 4, fp);
    write_u32_le(fp, data_bytes);

    Voice voices[MAX_VOICES];
    memset(voices, 0, sizeof(voices));
    uint32_t tick = 0;
    double to_next = 0.0;
    int16_t block[AUDIO_BLOCK * AUDIO_CHANNELS];

    for (uint32_t done = 0; done < frames; ) {
        int n = (frames - done > AUDIO_BLOCK) ? AUDIO_BLOCK : (int)(frames - done);
        memset(block, 0, (size_t)n * AUDIO_CHANNELS * sizeof(int16_t));

        for (int f = 0; f < n; f++) {
            if (to_next <= 0.0 && tick < last_tick) {
                for (int t = 0; t < p->track_count && t < MAX_TRACKS; t++) {
                    const Track *tr = &p->tracks[t];
                    if (tr->mute) continue;
                    for (int e = 0; e < tr->event_count; e++) {
                        const NoteEvent *ev = &tr->events[e];
                        if (ev->start_tick == tick) {
                            float gain = tr->volume * ((float)ev->velocity / 127.0f);
                            offline_trigger(voices, samples, p, t, ev->note, gain, ev->duration_tick);
                        }
                    }
                }
                tick++;
                to_next += frames_per_tick(p->bpm);
            }
            to_next -= 1.0;

            int mix = 0;
            for (int v = 0; v < MAX_VOICES; v++) {
                Voice *voice = &voices[v];
                if (!voice->active) continue;
                WavSample *sample = &samples[voice->track];
                if (voice->pos >= (double)(sample->frames - 1)) {
                    voice->active = 0;
                    continue;
                }
                if (voice->frames_left == 0) begin_release(voice);
                mix += (int)((float)sample_at(sample, voice->pos) *
                             voice->gain * voice_release_gain(voice));
                voice->pos += voice->inc;
                if (voice->frames_left > 0) voice->frames_left--;
                advance_voice_release(voice);
            }

            int16_t s = mix_to_i16(mix);
            block[f * 2] = s;
            block[f * 2 + 1] = s;
        }

        fwrite(block, sizeof(int16_t), (size_t)n * AUDIO_CHANNELS, fp);
        done += (uint32_t)n;
    }

    fclose(fp);
    for (int t = 0; t < MAX_TRACKS; t++) wav_free(&samples[t]);
    return 0;
}
