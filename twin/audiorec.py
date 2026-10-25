"""The twin's speaker: QEMU writes what the twin plays into a pipe (audio.fifo)
and this keeps the last few minutes of it, so the twin can play for hours
without filling the disk.

  audiorec.py record             follow audio.fifo (run.sh starts this before QEMU)
  audiorec.py save OUT.wav [S]   the last S seconds played (default 10) as a .wav
  audiorec.py level              loudness of the last half second (0 = silence)

Only sound the twin actually plays is kept: while nothing plays, QEMU writes
nothing, so "the last 10 seconds" means the last 10 seconds of sound.
"""
import collections
import math
import os
import socket
import struct
import sys
import threading

RUN = os.environ.get("TWIN_RUN", "/home/claude/twin/run")
FIFO = os.path.join(RUN, "audio.fifo")
SOCK = os.path.join(RUN, "audio.sock")
KEEP = 180                      # seconds kept


class Ring:
    def __init__(self):
        self.fmt = (44100, 2, 16)            # QEMU's default; read from the header
        self.chunks = collections.deque()
        self.size = 0
        self.lock = threading.Lock()

    def bps(self):
        rate, ch, bits = self.fmt
        return rate * ch * bits // 8

    def add(self, b):
        with self.lock:
            self.chunks.append(b)
            self.size += len(b)
            while self.chunks and self.size - len(self.chunks[0]) >= KEEP * self.bps():
                self.size -= len(self.chunks.popleft())

    def last(self, secs):
        with self.lock:
            data = b"".join(self.chunks)
        rate, ch, bits = self.fmt
        frame = ch * bits // 8
        n = min(len(data), int(secs * self.bps())) // frame * frame
        return data[len(data) - n:]


def wav_bytes(fmt, pcm):
    rate, ch, bits = fmt
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, ch, rate, rate * ch * bits // 8, ch * bits // 8, bits) +
            b"data" + struct.pack("<I", len(pcm)) + pcm)


def level(fmt, pcm):
    if len(pcm) < 4:
        return 0.0
    n = len(pcm) // 2
    v = struct.unpack("<%dh" % n, pcm[:n * 2])
    return math.sqrt(sum(x * x for x in v) / n) / 32768.0


def serve(ring):
    try:
        os.unlink(SOCK)
    except OSError:
        pass
    s = socket.socket(socket.AF_UNIX)
    s.bind(SOCK)
    s.listen(4)
    while True:
        c, _ = s.accept()
        try:
            req = c.makefile().readline().split()
            if req and req[0] == "save":
                pcm = ring.last(float(req[2]) if len(req) > 2 else 10)
                with open(req[1], "wb") as f:
                    f.write(wav_bytes(ring.fmt, pcm))
                c.sendall(b"ok %.1f s\n" % (len(pcm) / ring.bps()))
            elif req and req[0] == "level":
                c.sendall(b"%.4f\n" % level(ring.fmt, ring.last(0.5)))
            else:
                c.sendall(b"? save PATH [SECONDS] | level\n")
        except Exception as e:  # keep serving
            c.sendall(("error: %s\n" % e).encode())
        c.close()


def record():
    ring = Ring()
    threading.Thread(target=serve, args=(ring,), daemon=True).start()
    with open(FIFO, "rb", buffering=0) as f:   # waits for QEMU to open it
        head = b""
        while len(head) < 44:
            d = f.read(44 - len(head))
            if not d:
                return
            head += d
        if head[:4] == b"RIFF":
            ch, rate = struct.unpack("<HI", head[22:28])
            bits, = struct.unpack("<H", head[34:36])
            ring.fmt = (rate, ch, bits)
        else:
            ring.add(head)
        while True:
            d = f.read(65536)
            if not d:
                break                      # the twin stopped
            ring.add(d)


def ask(line):
    s = socket.socket(socket.AF_UNIX)
    s.connect(SOCK)
    s.sendall(line.encode() + b"\n")
    out = s.makefile().read()
    s.close()
    return out.strip()


def main(a):
    if a[0] == "record":
        record()
    elif a[0] == "save":
        print(ask("save %s %s" % (os.path.abspath(a[1]), a[2] if len(a) > 2 else 10)))
    elif a[0] == "level":
        print(ask("level"))


if __name__ == "__main__":
    main(sys.argv[1:])
