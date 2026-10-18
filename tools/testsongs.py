"""Three short songs made from scratch for testing the music player (no one
else's music, so they can go anywhere): chords, bass, an arpeggio and drums,
written as tagged MP3s with Zappo cover art.

usage: testsongs.py OUT_MUSIC_DIR ZAPPOS_DIR"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

RATE = 44100
SONGS = [
    # title, key (MIDI root), bpm, progression (scale degrees), zappo
    ("Electric Sparks", 57, 128, [0, 5, 3, 4], "electric-m.png"),
    ("Crystal Shards", 62, 96, [0, 3, 5, 4], "crystal-m.png"),
    ("Ember Run", 52, 140, [0, 6, 5, 4], "fire-m.png"),
]
MAJOR = [0, 2, 4, 5, 7, 9, 11]
MINOR = [0, 2, 3, 5, 7, 8, 10]


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def tone(f, dur, shape="tri", vol=0.3, decay=3.0):
    t = np.arange(int(dur * RATE)) / RATE
    ph = (t * f) % 1.0
    if shape == "tri":
        w = 4 * np.abs(ph - 0.5) - 1
    elif shape == "sq":
        w = np.where(ph < 0.5, 1.0, -1.0) * 0.6
    else:
        w = np.sin(2 * np.pi * f * t)
    env = np.exp(-t * decay) * np.minimum(1, t * 200)
    return w * env * vol


def song(root, bpm, prog, minor, seconds=48):
    sc = MINOR if minor else MAJOR
    beat = 60.0 / bpm
    out = np.zeros(int(seconds * RATE) + RATE)
    rng = np.random.default_rng(root)
    n_bars = int(seconds / (beat * 4))
    for bar in range(n_bars):
        deg = prog[bar % len(prog)]
        chord = [root + sc[(deg + k) % 7] + 12 * ((deg + k) // 7) for k in (0, 2, 4)]
        t0 = bar * 4 * beat
        for b in range(4):                                   # bass on every beat
            s = int((t0 + b * beat) * RATE)
            x = tone(hz(chord[0] - 12), beat * 0.9, "tri", 0.35, 4)
            out[s:s + len(x)] += x
        for st in range(8):                                  # arpeggio in eighths
            s = int((t0 + st * beat / 2) * RATE)
            note = chord[[0, 1, 2, 1, 2, 0, 1, 2][st]] + 12
            x = tone(hz(note), beat / 2, "sq", 0.10, 6)
            out[s:s + len(x)] += x
        if bar % 2 == 1:                                     # a little melody every other bar
            for st in range(4):
                s = int((t0 + st * beat) * RATE)
                note = root + 12 + sc[int(rng.integers(0, 7))]
                x = tone(hz(note), beat * 0.95, "sin", 0.16, 2)
                out[s:s + len(x)] += x
        for b in range(4):                                   # kick and hat
            s = int((t0 + b * beat) * RATE)
            t = np.arange(int(0.18 * RATE)) / RATE
            kick = np.sin(2 * np.pi * (50 + 90 * np.exp(-t * 30)) * t) * np.exp(-t * 18) * 0.5
            out[s:s + len(kick)] += kick
            s2 = int((t0 + b * beat + beat / 2) * RATE)
            hat = rng.standard_normal(int(0.04 * RATE)) * np.exp(-np.arange(int(0.04 * RATE)) / RATE * 90) * 0.06
            out[s2:s2 + len(hat)] += hat
    fade = np.linspace(1, 0, RATE * 3)
    out = out[: int(seconds * RATE)]
    out[-len(fade):] *= fade
    out /= max(1e-9, np.abs(out).max()) / 0.85
    return np.stack([out, np.roll(out, 220) * 0.9 + out * 0.1], 1)


def main(outdir, zappos):
    album = os.path.join(outdir, "Zappo Test Band", "Twin Sessions")
    os.makedirs(album, exist_ok=True)
    from PIL import Image
    for i, (title, root, bpm, prog, pic) in enumerate(SONGS):
        pcm = (song(root, bpm, prog, minor=(i == 1)) * 32767).astype(np.int16)
        wav = tempfile.mktemp(suffix=".wav")
        with wave.open(wav, "wb") as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes())
        cover = tempfile.mktemp(suffix=".jpg")
        im = Image.open(os.path.join(zappos, pic)).convert("RGBA")
        bg = Image.new("RGBA", (320, 320), (18, 22, 40, 255))
        im.thumbnail((300, 300))
        bg.alpha_composite(im, ((320 - im.width) // 2, (320 - im.height) // 2))
        bg.convert("RGB").save(cover, quality=88)
        if i == 0:
            bg.convert("RGB").save(os.path.join(album, "cover.jpg"), quality=88)
        out = os.path.join(album, "%02d %s.mp3" % (i + 1, title))
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", wav, "-i", cover, "-map", "0:a", "-map", "1:v",
                        "-c:a", "libmp3lame", "-b:a", "128k", "-c:v", "mjpeg", "-id3v2_version", "3",
                        "-metadata", "title=" + title, "-metadata", "artist=Zappo Test Band",
                        "-metadata", "album=Twin Sessions", "-metadata", "genre=Chiptune",
                        "-metadata", "track=%d" % (i + 1), "-metadata:s:v", "comment=Cover (front)", out], check=True)
        os.remove(wav); os.remove(cover)
        print(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
