# BashBeats

Terminal-based DAW and live performance tool written in C.

It includes a ncurses editor, piano-roll project format, live keyboard
performance mode, a simple sample-based audio engine, TCP PCM streaming, and WAV
export.

## Layout

```text
bashbeats/
├── include/      public headers
├── src/          application and audio engine sources
├── samples/      WAV instruments used by projects
├── saves/        example .bbeat projects
├── tools/        helper scripts
├── docs/         manual and reference material
└── Makefile
```

## Build

```bash
make
```

Optional UI-only test build:

```bash
make stub
```

Generate sample WAV files:

```bash
make samples
```

Clean build output:

```bash
make clean
```

## Run

```bash
./bashbeats
```

Open a project directly:

```bash
./bashbeats saves/testver23.bbeat
```

## Dependencies

Ubuntu/Debian:

```bash
sudo apt install build-essential libncurses-dev alsa-utils
```

`alsa-utils` provides `aplay`, which BashBeats uses for local playback when
available. WAV export and TCP streaming do not require ALSA development
headers.

## Controls And Manual

See [docs/MANUAL.md](docs/MANUAL.md) for the full Korean user manual.
