#!/usr/bin/env python3
"""Console pictures for Zappo X on Knulli: one per console Knulli knows (its
es_systems.cfg), from RetroArch's "Systematic" icons (libretro/retroarch-assets,
xmb/systematic/png, CC BY 4.0), as knulli/icons/<CODE>.png - the code being
Zappo's name for the console (src/data_knulli.inc), which is what the console
list looks up.

  knulli_icons.py ASSETS_CHECKOUT KNULLI_ES_SYSTEMS_CFG
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from console_icons import NAMES, SIZE  # noqa: E402  (the tablet's code -> icon names)

# Knulli folders whose icon isn't found from the code or the console's name
EXTRA = {
    "pico": "Sega - PICO", "naomi": "Sega - Naomi", "naomi2": "Sega - Naomi 2", "atomiswave": "Atomiswave",
    "openbor": "OpenBOR", "pico8": "PICO-8", "wswanc": "Bandai - WonderSwan Color",
    "ngpc": "SNK - Neo Geo Pocket Color", "ngp": "SNK - Neo Geo Pocket", "easyrpg": "RPG Maker",
    "j2me": "Mobile - J2ME", "c20": "Commodore - VIC-20", "megadrive": "Sega - Mega Drive - Genesis",
    "segacd": "Sega - Mega-CD - Sega CD", "sega32x": "Sega - 32X", "mastersystem": "Sega - Master System - Mark III",
    "gamegear": "Sega - Game Gear", "dreamcast": "Sega - Dreamcast", "saturn": "Sega - Saturn",
    "psx": "Sony - PlayStation", "ps2": "Sony - PlayStation 2", "psp": "Sony - PlayStation Portable",
    "fbneo": "FBNeo - Arcade Games", "mame": "MAME", "neogeo": "SNK - Neo Geo", "neogeocd": "SNK - Neo Geo CD",
    "ports": "Ports", "scummvm": "ScummVM", "dos": "DOS", "amiga500": "Commodore - Amiga",
    "amigacd32": "Commodore - CD32", "c64": "Commodore - 64", "c128": "Commodore - 128",
    "3ds": "Nintendo - Nintendo 3DS", "gamecube": "Nintendo - GameCube", "wii": "Nintendo - Wii",
    "pokemini": "Nintendo - Pokemon Mini", "sufami": "Nintendo - Sufami Turbo",
    "cplus4": "Commodore - Plus-4", "pet": "Commodore - PET", "videopacplus": "Philips - Videopac+",
    "o2em": "Magnavox - Odyssey2", "tic80": "TIC-80", "lowresnx": "LowRes NX", "lutro": "Lutro",
    "uzebox": "Uzebox", "vectrex": "GCE - Vectrex", "wasm4": "WASM-4", "arduboy": "Arduboy Inc - Arduboy",
    "supervision": "Watara - Supervision", "megaduck": "Welback - Mega Duck", "gp32": "GamePark - GP32",
    "cdi": "Philips - CD-i", "3do": "The 3DO Company - 3DO", "jaguar": "Atari - Jaguar", "lynx": "Atari - Lynx",
    "atari800": "Atari - 8-bit", "atari5200": "Atari - 5200", "atari7800": "Atari - 7800", "atarist": "Atari - ST",
    "atari2600": "Atari - 2600", "msx1": "Microsoft - MSX", "msx2": "Microsoft - MSX2", "zxspectrum": "Sinclair - ZX Spectrum",
    "zx81": "Sinclair - ZX 81", "amstradcpc": "Amstrad - CPC", "x68000": "Sharp - X68000", "pcfx": "NEC - PC-FX",
    "gameandwatch": "Handheld Electronic Game", "virtualboy": "Nintendo - Virtual Boy", "wswan": "Bandai - WonderSwan",
    "channelf": "Fairchild - Channel F", "intellivision": "Mattel - Intellivision", "colecovision": "Coleco - ColecoVision",
    "sg1000": "Sega - SG-1000", "prboom": "DOOM", "tyrquake": "Quake", "cannonball": "Cannonball",
    "pcengine": "NEC - PC Engine - TurboGrafx 16", "pcenginecd": "NEC - PC Engine CD - TurboGrafx-CD",
    "supergrafx": "NEC - PC Engine SuperGrafx", "satellaview": "Nintendo - Satellaview", "nds": "Nintendo - Nintendo DS",
    "n64": "Nintendo - Nintendo 64", "snes": "Nintendo - Super Nintendo Entertainment System",
    "nes": "Nintendo - Nintendo Entertainment System", "fds": "Nintendo - Family Computer Disk System",
    "gb": "Nintendo - Game Boy", "gbc": "Nintendo - Game Boy Color", "gba": "Nintendo - Game Boy Advance",
    "cps1": "Capcom - CP System I", "cps2": "Capcom - CP System II", "cps3": "Capcom - CP System III",
    "model2": "Sega - Model 2", "model3": "Sega - Model 3", "triforce": "Sega - Triforce", "zc210": "ZC",
}


def norm(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def main(assets, es_systems):
    src_dir = os.path.join(assets, "xmb", "systematic", "png")
    icons = {norm(f[:-4]): f for f in os.listdir(src_dir) if f.endswith(".png") and "-content" not in f}
    by_tail = {}
    for f in icons.values():
        tail = f[:-4].split(" - ")[-1]
        by_tail.setdefault(norm(tail), f)
    codes = dict(re.findall(r'\{"([a-z0-9]+)", "([A-Z0-9]+)"\}', open(os.path.join(HERE, "..", "src", "data_knulli.inc")).read()))
    txt = open(es_systems).read()
    out = os.path.join(HERE, "..", "knulli", "icons")
    os.makedirs(out, exist_ok=True)
    made, missing = 0, []
    for blk in re.findall(r"<system>(.*?)</system>", txt, re.S):
        m = re.search(r"<path>/userdata/roms/([^</]+)", blk)
        if not m:
            continue
        d = m.group(1)
        full = (re.search(r"<fullname>(.*?)</fullname>", blk) or re.search(r"<name>(.*?)</name>", blk)).group(1)
        code = codes.get(d, d.upper())
        cands = [EXTRA.get(d), NAMES.get(code), full]
        src = None
        for c in cands:
            if not c:
                continue
            f = icons.get(norm(c)) or by_tail.get(norm(c))
            if f:
                src = os.path.join(src_dir, f)
                break
        if not src:
            missing.append(d)
            continue
        im = Image.open(src).convert("RGBA")
        im = im.crop(im.getbbox() or (0, 0, im.width, im.height))
        im.thumbnail((SIZE, SIZE), Image.LANCZOS)
        im.save(os.path.join(out, code + ".png"), optimize=True)
        made += 1
    print("%d pictures; none for: %s" % (made, " ".join(missing) or "-"))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
