#!/usr/bin/env python3
import math
import wave
from pathlib import Path

SR = 44100
OUT = Path(__file__).resolve().parents[1] / 'samples'
OUT.mkdir(exist_ok=True)

def write_wav(name, data):
    p = OUT / name
    with wave.open(str(p), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for x in data:
            x = max(-1.0, min(1.0, x))
            frames += int(x * 32767).to_bytes(2, 'little', signed=True)
        w.writeframes(frames)
    print(p)

def kick():
    n = int(0.22 * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / SR
        f = 130 * math.exp(-18 * t) + 38
        phase += 2 * math.pi * f / SR
        env = math.exp(-14 * t)
        click = 0.5 * math.exp(-180 * t) * (1 if i % 2 == 0 else -1)
        out.append(0.9 * math.sin(phase) * env + click)
    return out

def snare():
    n = int(0.18 * SR)
    out = []
    seed = 1
    for i in range(n):
        t = i / SR
        seed = (1103515245 * seed + 12345) & 0x7fffffff
        noise = (seed / 0x7fffffff) * 2 - 1
        tone = math.sin(2 * math.pi * 190 * t)
        env = math.exp(-18 * t)
        out.append((0.75 * noise + 0.25 * tone) * env)
    return out

def hat():
    n = int(0.07 * SR)
    out = []
    seed = 7
    prev = 0.0
    for i in range(n):
        t = i / SR
        seed = (1664525 * seed + 1013904223) & 0xffffffff
        noise = (seed / 0xffffffff) * 2 - 1
        hp = noise - prev * 0.95
        prev = noise
        env = math.exp(-55 * t)
        out.append(0.55 * hp * env)
    return out

write_wav('kick.wav', kick())
write_wav('snare.wav', snare())
write_wav('hat.wav', hat())


def piano_c4():
    n = int(0.55 * SR)
    out = []
    f0 = 261.625565
    for i in range(n):
        t = i / SR
        # simple plucked electric-piano-ish tone with harmonics
        env = math.exp(-3.2 * t) * (1.0 - math.exp(-80*t))
        x = (0.72*math.sin(2*math.pi*f0*t) +
             0.28*math.sin(2*math.pi*2*f0*t) +
             0.12*math.sin(2*math.pi*3*f0*t))
        out.append(0.55 * x * env)
    return out

write_wav('piano.wav', piano_c4())
