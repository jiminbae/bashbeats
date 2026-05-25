# BashBeats

BashBeats is a terminal-based mini DAW (Digital Audio Workstation) written in C.
It runs inside an `ncurses` interface, lets users create tracks, place notes in
a piano-roll editor, and play songs using WAV sample instruments.

It includes:

- Terminal-based DAW editor
- Track creation, deletion, mute, and volume controls
- Piano-roll note editing
- BPM control and playback seeking
- WAV sample-based instrument playback
- Live keyboard Performance Mode
- `.bbeat` project save and load support
- WAV export
- TCP PCM streaming

## Project Layout

```text
bashbeats/
├── include/      Header files
├── src/          Main program, editor, and audio engine sources
├── samples/      Default WAV instrument samples
├── saves/        Example and saved .bbeat projects
├── tools/        Helper scripts
├── docs/         Manual and reference documents
├── Makefile      Build script
└── README.md
```

## Requirements

On Ubuntu/Debian, install the required packages with:

```bash
sudo apt install build-essential libncurses-dev alsa-utils
```

`alsa-utils` provides `aplay`, which BashBeats uses for local audio playback.
WAV export and TCP streaming do not require ALSA development headers, but
installing `aplay` is recommended if you want to hear sound directly from the
terminal application.

## Build

Build BashBeats with the real audio engine:

```bash
make
```

Build a UI-only version with the audio stub:

```bash
make stub
```

Regenerate the default WAV sample files:

```bash
make samples
```

Remove build output:

```bash
make clean
```

## Run

Run BashBeats from the project root:

```bash
./bashbeats
```

Open a saved project directly:

```bash
./bashbeats saves/full_band_demo.bbeat
```

When the program starts, the intro screen lets you create a new project, enter
Performance Mode, or open an existing `.bbeat` file.

## Quick Controls

### Intro Screen

- `Up` / `Down`: Move selection
- `Enter`: Open selected item
- Type text: Filter saved project files
- `Backspace`: Delete one search character
- `Ctrl+C`: Show quit confirmation

### TRACK Mode

TRACK mode is the main project overview where tracks are managed.

- `Enter`: Open the selected track in EDIT mode
- `Space`: Play / pause
- `Up` / `Down`: Select track
- `Left` / `Right`: Move playback position
- `a`: Add track
- `d`: Delete track
- `i`: Change instrument
- `b`: Change base note
- `m`: Toggle mute
- `+` / `-`: Adjust volume
- `Esc`: Return to intro screen
- `Ctrl+F`: Open FILE mode
- `Ctrl+C`: Show quit confirmation

### EDIT Mode

EDIT mode is the piano-roll note editor.

- `Arrow keys`: Move cursor
- `Enter`: Add or toggle a note at the cursor
- `Delete`: Remove a note at the cursor
- `Space`: Play from the current cursor position / pause
- `[` / `]`: Switch editing track
- `+` / `-`: Adjust BPM
- `,` then `.`: Create a longer note range
- `Esc`: Return to TRACK mode
- `Ctrl+F`: Open FILE mode
- `Ctrl+C`: Show quit confirmation

If you move the cursor while playback is running, BashBeats pauses playback,
clears currently sounding notes, and retimes playback to the new cursor
position. Press `Space` again to continue from that point.

### FILE Mode

FILE mode handles project file actions.

- `S`: Save project
- `L`: Load a saved `.bbeat` project
- `N`: Create a new project
- `R`: Change save path
- `T`: Change project title
- `E`: Export as WAV
- `Esc`: Return to TRACK mode
- `Ctrl+C`: Show quit confirmation

### Performance Mode

Performance Mode turns the computer keyboard into a playable instrument.

- `z x c v b n m`: Lower octave white keys
- `s d g h j`: Lower octave black keys
- `q w e r t y u`: Upper octave white keys
- `2 3 5 6 7`: Upper octave black keys
- `Up` / `Down`: Change octave
- `i`: Change instrument
- `Esc`: Return to intro screen
- `Ctrl+C`: Show quit confirmation

## Example Projects

The `saves/` directory includes test projects and demo songs.

- `saves/full_band_demo.bbeat`: Demo song using all default instruments
- `saves/testver23.bbeat`: Existing test project
- `saves/testbashb.bbeat`: Existing test project
- `saves/Untitled.bbeat`: Basic saved project example

To hear the full-band demo:

```bash
make
./bashbeats saves/full_band_demo.bbeat
```

## File Format

BashBeats uses its own text-based `.bbeat` project format. A project file stores
the project title, BPM, track list, instrument paths, volume, mute state, and
note events.

Instrument samples are 16-bit PCM WAV files. Default samples are included in
the `samples/` directory and can be regenerated with
`tools/generate_samples.py`.

## Developers

| Name | Role |
|---|---|
| Jimin Bae | Core implementation: C application logic, editor features, audio integration, file I/O, and build setup |
| Changwoo Ha | Planning and design: project concept, user workflow, terminal UI direction, and feature prioritization |


## Manual

See [docs/MANUAL.md](docs/MANUAL.md) for the full user manual.
