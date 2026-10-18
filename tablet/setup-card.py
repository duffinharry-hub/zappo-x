#!/usr/bin/env python3
"""Readies Zappo X's card on the tablet (tablet/install.sh runs it).

  setup-card.py CARD [OLD_CARD]

- OLD_CARD (the card from before, in Termux's own folder): your settings,
  favourites and anything you added come across to CARD, once, and the old
  one is left as it was. The pretend games and emulators stay behind.
- Every console RetroArch for Android plays is added to the card's console
  list (tablet/cores-android.json); consoles already there keep their emulator.
- Once: consoles without games are hidden, so the list shows only yours.
- Each console gets its folder (Roms/GBA, Roms/NDS ...), as on a GarlicOS
  card: Zappo offers those consoles when you choose one for something in Import.
- Each console gets its picture in the console list (tablet/icons); one you
  put in CFW/clove/icons yourself is kept.
- Games that ended up in the wrong place (a DS game in BIOS, a Game Boy
  Advance one in Game Boy - older Zappo didn't know DS games, and the BIOS
  folder came first in its list) go to their console.
"""
import zipfile
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
card = os.path.realpath(sys.argv[1])
old = os.path.realpath(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else ""
clove = os.path.join(card, "mmc", "CFW", "clove")
os.makedirs(clove, exist_ok=True)


def stand_ins():
    """the pretend games and emulators on the test card (relative paths)"""
    try:
        idx = json.load(open(os.path.join(HERE, "card", "index.json"), encoding="utf-8"))
    except (OSError, ValueError):
        return set()
    return {it["p"].lstrip("/") for it in idx if "b" in it}


# 1. what was on the old card
if old and old != card and os.path.isdir(os.path.join(old, "mmc")) and not os.path.exists(os.path.join(clove, "settings.json")):
    skip = stand_ins()
    copied = 0
    for dirpath, dirs, files in os.walk(old):
        rel_dir = os.path.relpath(dirpath, old)
        for fn in files:
            rel = os.path.normpath(os.path.join(rel_dir, fn))
            if rel.replace(os.sep, "/") in skip:
                continue
            dst = os.path.join(card, rel)
            if os.path.exists(dst):
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            try:
                shutil.copyfile(os.path.join(dirpath, fn), dst)
                copied += 1
            except OSError as e:
                print("  (couldn't copy %s: %s)" % (rel, e))
    # favourites, recents ... named games by where the old card was
    for fn in os.listdir(clove):
        if fn.endswith((".json", ".tsv", ".txt", ".m3u")):
            p = os.path.join(clove, fn)
            try:
                t = open(p, encoding="utf-8").read()
            except (OSError, UnicodeDecodeError):
                continue
            if old in t:
                open(p, "w", encoding="utf-8").write(t.replace(old, card))
    print("Your settings and files came across from %s (%d files); that folder is left as it was." % (old, copied))

# 2. every console RetroArch for Android plays
cm_path = os.path.join(card, "mmc", "CFW", "config", "coremapping.json")
os.makedirs(os.path.dirname(cm_path), exist_ok=True)
try:
    cm = json.load(open(cm_path, encoding="utf-8"))
except (OSError, ValueError):
    cm = {}
added = 0
for code, core in json.load(open(os.path.join(HERE, "cores-android.json"), encoding="utf-8")).items():
    if code not in cm:
        cm[code] = core
        added += 1
if added:
    json.dump(cm, open(cm_path, "w", encoding="utf-8"), indent=1)
    print("%d more consoles added (%d in all)." % (added, len(cm)))

# 3. once: only the consoles you have games for
mark = os.path.join(clove, ".tablet-setup-1")
if not os.path.exists(mark):
    sp = os.path.join(clove, "settings.json")
    try:
        st = json.load(open(sp, encoding="utf-8"))
        st["empty_consoles"] = 1
        json.dump(st, open(sp, "w", encoding="utf-8"), indent=1)
    except (OSError, ValueError):
        pass  # (no settings yet: the tablet's default already hides them)
    open(mark, "w").close()

# 4. a folder for every console
roms = os.path.join(card, "mmc", "Roms")
for code in cm:
    os.makedirs(os.path.join(roms, code), exist_ok=True)

# 5. a picture for every console (RetroArch's Systematic icons, tablet/icons)
icons = os.path.join(clove, "icons")
os.makedirs(icons, exist_ok=True)
for fn in sorted(os.listdir(os.path.join(HERE, "icons"))):
    if fn.endswith(".png") and not os.path.exists(os.path.join(icons, fn)):
        shutil.copyfile(os.path.join(HERE, "icons", fn), os.path.join(icons, fn))

# 6. games in the wrong place
GAME_EXT = {".nds": "NDS", ".gba": "GBA", ".gb": "GB", ".gbc": "GBC", ".nes": "FC", ".sfc": "SFC", ".smc": "SFC",
            ".md": "MD", ".gen": "MD", ".smd": "MD", ".sms": "MS", ".gg": "GG", ".pce": "PCE", ".ngp": "NGP",
            ".ngc": "NGP", ".ws": "WS", ".wsc": "WS", ".z64": "N64", ".n64": "N64", ".v64": "N64", ".lnx": "LYNX"}
# (a Game Boy Color folder may hold Game Boy games: only clear mistakes move)
WRONG = {"GB": {"GBA", "NDS"}, "GBC": {"GBA", "NDS"}, "GBA": {"NDS"}}


def console_of(path):
    """the console a game is for, from its name or what's in its .zip"""
    ext = os.path.splitext(path)[1].lower()
    if ext in GAME_EXT:
        return GAME_EXT[ext]
    if ext != ".zip":
        return None
    try:
        with zipfile.ZipFile(path) as z:
            found = {GAME_EXT.get(os.path.splitext(n)[1].lower()) for n in z.namelist() if not n.endswith("/")}
    except (OSError, zipfile.BadZipFile):
        return None
    found.discard(None)
    return found.pop() if len(found) == 1 else None


def move(src, code, why):
    dst_dir = os.path.join(roms, code)
    os.makedirs(dst_dir, exist_ok=True)
    dst = os.path.join(dst_dir, os.path.basename(src))
    if os.path.exists(dst):
        return
    shutil.move(src, dst)
    print("Moved %s %s to Roms/%s." % (os.path.basename(src), why, code))


bios = os.path.join(card, "mmc", "BIOS")
if os.path.isdir(bios):
    for fn in sorted(os.listdir(bios)):
        p = os.path.join(bios, fn)
        if os.path.isfile(p) and os.path.splitext(fn)[1].lower() in (".zip", ".nds", ".gba"):
            c = console_of(p)
            if c in cm:
                move(p, c, "from BIOS (it's a game)")
for here, wrong in WRONG.items():
    d = os.path.join(roms, here)
    if not os.path.isdir(d):
        continue
    for fn in sorted(os.listdir(d)):
        p = os.path.join(d, fn)
        if os.path.isfile(p):
            c = console_of(p)
            if c in wrong and c in cm:
                move(p, c, "from Roms/" + here)
