#!/usr/bin/env python3
import math
import wave
from pathlib import Path

SR = 44100
OUT = Path(__file__).resolve().parents[1] / "samples"
OUT.mkdir(exist_ok=True)


def clamp(x, lo=-1.0, hi=1.0):
    return max(lo, min(hi, x))


def soft_clip(x):
    return math.tanh(1.35 * x) / math.tanh(1.35)


def fade_edges(data, fade_ms=4.0):
    n = len(data)
    fade = min(n // 2, int(SR * fade_ms / 1000.0))
    if fade <= 0:
        return data
    out = list(data)
    for i in range(fade):
        a = i / fade
        out[i] *= a * a
        out[n - 1 - i] *= a * a
    return out


def normalize(data, peak=0.82):
    mx = max((abs(x) for x in data), default=0.0)
    if mx <= 1e-9:
        return data
    scale = peak / mx
    return [x * scale for x in data]


def write_wav(name, data, peak=0.82):
    p = OUT / name
    data = normalize(fade_edges(data), peak)
    with wave.open(str(p), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for x in data:
            x = clamp(soft_clip(x), -0.98, 0.98)
            frames += int(x * 32767).to_bytes(2, "little", signed=True)
        w.writeframes(frames)
    print(p)


def lcg(seed):
    while True:
        seed = (1664525 * seed + 1013904223) & 0xFFFFFFFF
        yield (seed / 0xFFFFFFFF) * 2.0 - 1.0


def lowpass(data, alpha):
    out = []
    y = 0.0
    for x in data:
        y += alpha * (x - y)
        out.append(y)
    return out


def highpass(data, alpha):
    out = []
    y = 0.0
    prev_x = 0.0
    for x in data:
        y = alpha * (y + x - prev_x)
        prev_x = x
        out.append(y)
    return out


def kick():
    n = int(0.42 * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / SR
        pitch = 102.0 * math.exp(-22.0 * t) + 43.0
        phase += 2.0 * math.pi * pitch / SR
        body_env = math.exp(-9.0 * t)
        sub = 0.55 * math.sin(phase) * body_env
        tone = 0.18 * math.sin(phase * 0.5) * math.exp(-5.0 * t)
        click = 0.08 * math.sin(2.0 * math.pi * 1800.0 * t) * math.exp(-115.0 * t)
        out.append(sub + tone + click)
    return out


def snare():
    n = int(0.34 * SR)
    noise = []
    rng = lcg(0x51A7E)
    prev = 0.0
    for i in range(n):
        t = i / SR
        raw = next(rng)
        prev = 0.72 * prev + 0.28 * raw
        env = math.exp(-13.0 * t)
        tone = math.sin(2.0 * math.pi * 185.0 * t) * math.exp(-16.0 * t)
        noise.append(0.36 * prev * env + 0.22 * tone)
    return highpass(lowpass(noise, 0.55), 0.985)


def hat():
    n = int(0.13 * SR)
    rng = lcg(0xC0FFEE)
    raw = []
    for i in range(n):
        t = i / SR
        env = math.exp(-42.0 * t)
        metallic = (
            0.18 * math.sin(2.0 * math.pi * 6200.0 * t) +
            0.13 * math.sin(2.0 * math.pi * 8300.0 * t)
        )
        raw.append((0.24 * next(rng) + metallic) * env)
    return highpass(raw, 0.992)


def epiano(f0=261.625565, seconds=1.65, brightness=1.0):
    n = int(seconds * SR)
    out = []
    detune = 1.006
    for i in range(n):
        t = i / SR
        attack = 1.0 - math.exp(-85.0 * t)
        decay = 0.72 * math.exp(-2.15 * t) + 0.28 * math.exp(-0.72 * t)
        trem = 1.0 + 0.012 * math.sin(2.0 * math.pi * 5.2 * t)
        env = attack * decay * trem
        x = (
            0.56 * math.sin(2.0 * math.pi * f0 * t) +
            0.30 * math.sin(2.0 * math.pi * f0 * detune * t) +
            0.15 * brightness * math.sin(2.0 * math.pi * 2.0 * f0 * t + 0.25) +
            0.07 * brightness * math.sin(2.0 * math.pi * 3.0 * f0 * t)
        )
        out.append(0.58 * x * env)
    return lowpass(out, 0.32)


def bass():
    n = int(0.9 * SR)
    out = []
    f0 = 65.406391
    for i in range(n):
        t = i / SR
        env = (1.0 - math.exp(-65.0 * t)) * math.exp(-2.6 * t)
        x = (
            0.68 * math.sin(2.0 * math.pi * f0 * t) +
            0.24 * math.sin(2.0 * math.pi * 2.0 * f0 * t) +
            0.08 * math.sin(2.0 * math.pi * 3.0 * f0 * t)
        )
        out.append(0.62 * x * env)
    return lowpass(out, 0.22)


def silent():
    return [0.0] * int(0.05 * SR)


write_wav("kick.wav", kick(), peak=0.86)
write_wav("snare.wav", snare(), peak=0.70)
write_wav("hat.wav", hat(), peak=0.48)
write_wav("piano.wav", epiano(), peak=0.78)
write_wav("test1.wav", epiano(f0=329.627557, seconds=1.35, brightness=0.75), peak=0.70)
write_wav("bass.wav", bass(), peak=0.78)
write_wav("silent.wav", silent(), peak=0.0)
