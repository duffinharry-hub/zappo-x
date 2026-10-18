"""Lays out the browser preview's memory card: the kit's themes, fonts and
art, plus test files (songs made by tools/testsongs.py, the charging-scene
film, Blockbox worlds and stand-in games), as files the page fetches.

OUT_DIR/card/index.json lists [{"p": path on the card, "f": file to fetch}]
or, for the tiny files of types a web page can't be served (.gb, .so ...),
[{"p": path, "b": base64 contents}].  Fetched files are numbered, keeping
their extension (pictures, fonts, songs, the film, JSON, text).

usage: webcard.py OUT_DIR  (run from the repo root, after testsongs.py)"""
import json
import os
import sys

KIT = "package/CFW/clove"
SKIP_KIT = {"clove", "main.sh"}             # the RG35XX program and its start script

# stand-in games: empty files with honest names, so the consoles fill up and
# choosing one shows what the handheld would start
GAMES = {
    "GB": ["Test Cart A.gb", "Test Cart B.gb"],
    "GBC": ["Colour Test.gbc"],
    "GBA": ["Advance Test.gba", "Sprite Test.gba", "Sound Test.gba"],
    "FC": ["Eight Bit Test.nes"],
    "SFC": ["Mode 7 Test.sfc"],
    "MD": ["Blast Processing Test.md"],
    "PS": ["Disc Test.chd"],
}
CORES = {"GB": "gambatte_libretro.so", "GBC": "gambatte_libretro.so", "GBA": "mgba_libretro.so",
         "FC": "fceumm_libretro.so", "SFC": "snes9x_libretro.so", "MD": "genesis_plus_gx_libretro.so",
         "PS": "pcsx_rearmed_libretro.so", "VIDEOS": "ffmpeg_libretro.so"}


def files():
    for d, dirs, fs in os.walk(KIT):
        dirs.sort()
        for f in sorted(fs):
            rel = os.path.relpath(os.path.join(d, f), KIT)
            if rel in SKIP_KIT or rel.endswith(".example"):
                continue
            yield "/mmc/CFW/clove/" + rel, open(os.path.join(d, f), "rb").read()
    for sub in ["Roms/BLOCKBOX", "Import"]:
        for f in sorted(os.listdir(os.path.join("package", sub))):
            yield "/mmc/%s/%s" % (sub, f), open(os.path.join("package", sub, f), "rb").read()
    for f in ["FiraSansCondensed-Regular.ttf", "NotoSans-Regular.ttf"]:   # GarlicOS's default fonts (OFL)
        yield "/mmc/CFW/font/" + f, open("third_party/fonts/" + f, "rb").read()
    yield "/mmc/CFW/config/coremapping.json", json.dumps(CORES, indent=1).encode()
    for core in sorted(set(CORES.values())) + ["blockbox_libretro.so"]:
        yield "/mmc/CFW/retroarch/.retroarch/cores/" + core, b"stand-in core for the browser preview\n"
    yield "/mmc/CFW/retroarch/retroarch", b"stand-in\n"
    for sys_, games in GAMES.items():
        for g in games:
            yield "/mmc/Roms/%s/%s" % (sys_, g), b"stand-in game for the browser preview\n"
    yield "/mmc/Roms/VIDEOS/Zappo charging scene.mp4", open("art/zappo_charging_scene.mp4", "rb").read()
    songs = "build/testsongs"
    for d, dirs, fs in os.walk(songs):
        dirs.sort()
        for f in sorted(fs):
            p = os.path.join(d, f)
            yield "/mmc/Music/" + os.path.relpath(p, songs), open(p, "rb").read()
    yield "/mmc/Zappo X preview.txt", (
        b"Zappo X running in a browser: the menus, music and look of the real thing.\n"
        b"Games are stand-ins here: choosing one shows what the RG40XX H would start.\n")


SERVED = {".png", ".jpg", ".jpeg", ".ttf", ".otf", ".mp3", ".mp4", ".json", ".txt", ".ogg", ".wav", ".webp"}


def main(out):
    import base64
    import shutil
    d = os.path.join(out, "card")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    index, total = [], 0
    for i, (path, data) in enumerate(files()):
        ext = os.path.splitext(path)[1].lower()
        if ext in SERVED:
            name = "f%03d%s" % (i, ext)
            open(os.path.join(d, name), "wb").write(data)
            index.append({"p": path, "f": name, "s": len(data)})
        else:
            assert len(data) < 65536, path
            index.append({"p": path, "b": base64.b64encode(data).decode()})
        total += len(data)
    json.dump(index, open(os.path.join(d, "index.json"), "w"), separators=(",", ":"))
    print("%s: %d files, %.1f MB" % (d, len(index), total / 1e6))


if __name__ == "__main__":
    main(sys.argv[1])
