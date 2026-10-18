#!/usr/bin/env python3
"""Console pictures for the tablet's console list: RetroArch's "Systematic"
icons (libretro/retroarch-assets, xmb/systematic/png, CC BY 4.0), one per
GarlicOS console folder, made smaller, as tablet/icons/<CODE>.png.

  console_icons.py ASSETS_CHECKOUT   (a checkout of libretro/retroarch-assets)
"""
import json
import os
import sys

from PIL import Image

# GarlicOS folder -> the icon's name (the libretro system name)
NAMES = {
    "GB": "Nintendo - Game Boy", "GBC": "Nintendo - Game Boy Color", "SGB": "Nintendo - Game Boy",
    "GBA": "Nintendo - Game Boy Advance", "GPSP": "Nintendo - Game Boy Advance",
    "FC": "Nintendo - Nintendo Entertainment System", "NESTOPIA": "Nintendo - Nintendo Entertainment System",
    "FDS": "Nintendo - Family Computer Disk System", "SFC": "Nintendo - Super Nintendo Entertainment System",
    "SUFAMI": "Nintendo - Sufami Turbo", "SATELLAVIEW": "Nintendo - Satellaview", "VB": "Nintendo - Virtual Boy",
    "POKE": "Nintendo - Pokemon Mini", "N64": "Nintendo - Nintendo 64", "NDS": "Nintendo - Nintendo DS",
    "GW": "Handheld Electronic Game",
    "MD": "Sega - Mega Drive - Genesis", "MS": "Sega - Master System - Mark III", "GG": "Sega - Game Gear",
    "SEGACD": "Sega - Mega-CD - Sega CD", "THIRTYTWOX": "Sega - 32X", "SEGASGONE": "Sega - SG-1000",
    "DC": "Sega - Dreamcast", "SATURN": "Sega - Saturn", "VMU": "Sega - Dreamcast (Visual Memory Unit)",
    "PS": "Sony - PlayStation", "PSP": "Sony - PlayStation Portable",
    "PCE": "NEC - PC Engine - TurboGrafx 16", "PCECD": "NEC - PC Engine CD - TurboGrafx-CD",
    "SGFX": "NEC - PC Engine SuperGrafx", "PCFX": "NEC - PC-FX", "PCEIGHTYEIGHT": "NEC - PC-8001 - PC-8801",
    "PCNINETYEIGHT": "NEC - PC-98",
    "NEOGEO": "SNK - Neo Geo", "NEOCD": "SNK - Neo Geo CD", "NGP": "SNK - Neo Geo Pocket Color",
    "WS": "Bandai - WonderSwan Color",
    "LYNX": "Atari - Lynx", "ATARI": "Atari - 2600", "FIFTYTWOHUNDRED": "Atari - 5200",
    "SEVENTYEIGHTHUNDRED": "Atari - 7800", "EIGHTHUNDRED": "Atari - 8-bit Family", "JAGUAR": "Atari - Jaguar",
    "ATARIST": "Atari - ST",
    "PANASONIC": "The 3DO Company - 3DO", "COLECO": "Coleco - ColecoVision", "INTELLIVISION": "Mattel - Intellivision",
    "VECTREX": "GCE - Vectrex", "ODYSSEY": "Magnavox - Odyssey2", "VIDEOPAC": "Magnavox - Odyssey2",
    "FAIRCHILD": "Fairchild - Channel F", "SUPERVISION": "Watara - Supervision", "MEGADUCK": "Welback - Mega Duck",
    "COMMODORE": "Commodore - 64", "C128": "Commodore - 128", "VIC20": "Commodore - VIC-20", "PET": "Commodore - PET",
    "PLUS4": "Commodore - Plus-4", "AMIGA": "Commodore - Amiga", "AMIGACD": "Commodore - CD32",
    "CPC": "Amstrad - CPC", "ZXS": "Sinclair - ZX Spectrum", "ZXEIGHTYONE": "Sinclair - ZX 81",
    "MSX": "Microsoft - MSX", "X68000": "Sharp - X68000", "XONE": "Sharp - X1",
    "ARCADE": "MAME", "MAME2000": "MAME 2000", "FBNEO": "FBNeo - Arcade Games", "FBA2012": "FBNeo - Arcade Games",
    "CPS1": "Capcom - CP System I", "CPS2": "Capcom - CP System II", "CPS3": "Capcom - CP System III",
    "DOS": "DOS", "DOOM": "DOOM", "QUAKE": "Quake", "SCUMMVM": "ScummVM", "TIC": "TIC-80",
    "UZEBOX": "Uzebox", "VIDEOS": "FFmpeg",
}
SIZE = 192


def main(assets):
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "tablet", "icons")
    os.makedirs(out, exist_ok=True)
    codes = json.load(open(os.path.join(here, "..", "tablet", "cores-android.json")))
    missing = []
    for code in sorted(set(codes) | set(NAMES)):
        name = NAMES.get(code)
        src = os.path.join(assets, "xmb", "systematic", "png", "%s.png" % name) if name else ""
        if not name or not os.path.exists(src):
            missing.append(code)
            continue
        im = Image.open(src).convert("RGBA")
        im = im.crop(im.getbbox() or (0, 0, im.width, im.height))
        im.thumbnail((SIZE, SIZE), Image.LANCZOS)
        im.save(os.path.join(out, code + ".png"), optimize=True)
    print("%d pictures; none for: %s" % (len([f for f in os.listdir(out) if f.endswith(".png")]), " ".join(missing) or "-"))


if __name__ == "__main__":
    main(sys.argv[1])
