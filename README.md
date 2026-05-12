# BashBeats

> Real-time Terminal DAW (Digital Audio Workstation)  
> ELEC462 System Programming Team Project  
> Kyungpook National University

---

## Overview

BashBeats is a real-time terminal-based DAW developed in C for Linux environments.

The project focuses on integrating core system programming concepts into a practical multimedia application, including:

- pthread-based concurrency
- TCP socket streaming
- low-level audio synthesis
- terminal UI rendering
- real-time keyboard input
- file I/O using system calls

Unlike conventional GUI DAWs, BashBeats is entirely keyboard-driven and runs directly inside the terminal.

---

# Features

## Real-time Step Sequencer

- 16-step drum sequencer
- KICK / SNARE / HAT tracks
- Real-time editing during playback
- Active step animation
- Piano roll support

---

## One-Octave Piano Roll

Supports:

- C
- D
- E
- F
- G
- A
- B

Users can compose melodies directly from the terminal.

---

## Real-time Audio Synthesis

- WAV sample parsing
- PCM audio mixing
- Volume control
- Pitch shifting
- Multi-track mixing

---

## TCP Audio Streaming

Server generates PCM audio and streams it to multiple clients using TCP sockets.

Supports:
- real-time playback
- multi-client broadcasting
- WSL-compatible stdout streaming

---

## Session Save / Load

Projects can be saved into custom `.daw` session files.

Supported operations:
- save
- load
- export WAV

---

## Terminal UI

- ncurses-based interface
- colorized UI
- transport status
- track highlighting
- animated playhead
- VU meters

---

# Demo

## Server

```bash
./bashbeats_server samples/kick.wav \
                    samples/snare.wav \
                    samples/hat.wav \
                    samples/piano.wav
```

## Client (Linux)

```bash
./bashbeats_client 127.0.0.1 7777
```

## Client (WSL Recommended)

```bash
./bashbeats_client 127.0.0.1 7777 --stdout | \
aplay -q -f S16_LE -c 1 -r 44100 -B 40000 -F 10000
```

---

# Build

## Requirements

Ubuntu 24.04 recommended

Required packages:

```bash
sudo apt update
sudo apt install build-essential libncurses5-dev alsa-utils libasound2-dev
```

---

## Compile

```bash
make clean
make
```

---

## Generate Sample WAV Files

```bash
make samples
```

This creates:

```text
samples/
 ├── kick.wav
 ├── snare.wav
 ├── hat.wav
 └── piano.wav
```

---

# Controls

| Key | Action |
|---|---|
| ← / → | Move step cursor |
| ↑ / ↓ | Change selected track |
| SPACE | Play / Pause |
| x | Toggle step |
| + / - | BPM control |
| v / V | Volume up/down |
| p / P | Pitch up/down |
| s | Save session |
| l | Load session |
| e | Export WAV |
| q | Quit |

---

# Architecture

```text
                +----------------+
                | Input Thread   |
                +----------------+
                         |
                         v
+------------+    +-------------+    +----------------+
| Audio      | -> | Ring Buffer | -> | Stream Thread  |
| Engine     |    +-------------+    +----------------+
+------------+                             |
                                           v
                                  TCP Socket Clients
```

---

# System Programming Concepts Used

## Threads

- pthread_create
- pthread_mutex_lock
- pthread_mutex_unlock

Used for:
- audio engine
- keyboard input
- streaming
- networking

---

## Socket Programming

- socket
- bind
- listen
- accept
- send
- recv

Used for real-time PCM streaming.

---

## File I/O

- open
- read
- write
- close
- lseek

Used for:
- WAV parsing
- session save/load
- WAV export

---

## Terminal Control

- ncurses
- termios raw mode

Used for:
- TUI rendering
- real-time keyboard input

---

## Signal Handling

- SIGINT
- SIGPIPE

Used for:
- graceful shutdown
- client disconnect handling

---

# Project Structure

```text
bashbeats/
 ├── src/
 │    ├── server.c
 │    ├── client.c
 │    ├── audio.c
 │    ├── wav.c
 │    ├── session.c
 │    ├── tui.c
 │    └── network.c
 │
 ├── samples/
 │
 ├── Makefile
 └── README.md
```

---

# Technical Highlights

- Real-time audio generation in pure C
- Thread-safe shared state using mutexes
- Low-latency terminal interaction
- Multi-client network audio streaming
- Terminal-based DAW workflow

---

# Development Environment

- OS: Ubuntu 24.04 / WSL2
- Language: C
- Libraries:
  - pthread
  - ncurses
  - ALSA

---

# Authors

Team 21

- JiMin Bae
- ChangWoo Ha

Kyungpook National University  
ELEC462 System Programming
