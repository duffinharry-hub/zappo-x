#!/usr/bin/python3
"""Box art for Zappo X on Knulli, from the libretro thumbnail library.

For every game that has no picture yet, looks the game up in the free libretro
thumbnail library (thumbnails.libretro.com, the pictures RetroArch itself uses)
and saves
    /userdata/roms/<console>/images/<game>-image.png
which is where Zappo X and Knulli's own menu both look. Pictures are made
smaller (at most 320 x 320) so lists stay quick on the handheld, and the game's
entry in Knulli's gamelist.xml gets the picture too (the first change keeps a
copy of the old gamelist beside it).

A picture is only saved when the game's name matches a name in the library
(ignoring region and version tags). ROM hacks and homebrew usually have none,
so they're looked up on TheGamesDB (thegamesdb.net) instead. Pictures already
there are never replaced.

  get-box-art.py [--systems gba,snes] [--roms DIR] [--source URL|DIR] [--no-tgdb]

On a tablet (Zappo X in Termux) the card is laid out as GarlicOS's:
  get-box-art.py --layout garlic --roms ~/zappo-card/mmc/Roms --home DIR
saves Roms/<CONSOLE>/Imgs/<game>.png instead, and there is no gamelist.

Zappo X starts it from Settings > Games > Get box art and shows how it's going
from the status file (/tmp/zappo-x-art.status, "key=value" lines). It runs at
low priority, so it can keep going while a game is played.
Nothing but the Python standard library, plus Pillow (part of Knulli) to make
the pictures smaller.
"""
import argparse
import glob
import html
import os
import re
import signal
import sys
import threading
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed

try:
    from PIL import Image
except ImportError:  # pictures are saved as they come
    Image = None

HOME = "/userdata/system/zappo-x"
ES_SYSTEMS = ["/usr/share/emulationstation/es_systems.cfg", "/userdata/system/configs/emulationstation/es_systems_*.cfg"]
ARCADE_NAMES = "/usr/share/emulationstation/resources/arcaderoms.xml"
AGENT = "Zappo X box art"

# Knulli's folder -> the libretro thumbnail collections to look in
LIBRETRO = {
    "gb": ["Nintendo - Game Boy"], "gbc": ["Nintendo - Game Boy Color"],
    "sgb": ["Nintendo - Game Boy", "Nintendo - Game Boy Color"],
    "gba": ["Nintendo - Game Boy Advance"], "nds": ["Nintendo - Nintendo DS"],
    "nes": ["Nintendo - Nintendo Entertainment System"], "fds": ["Nintendo - Family Computer Disk System"],
    "snes": ["Nintendo - Super Nintendo Entertainment System"], "sufami": ["Nintendo - Sufami Turbo"],
    "satellaview": ["Nintendo - Satellaview"], "virtualboy": ["Nintendo - Virtual Boy"],
    "pokemini": ["Nintendo - Pokemon Mini"], "n64": ["Nintendo - Nintendo 64"],
    "megadrive": ["Sega - Mega Drive - Genesis"], "mastersystem": ["Sega - Master System - Mark III"],
    "gamegear": ["Sega - Game Gear"], "segacd": ["Sega - Mega-CD - Sega CD"], "sega32x": ["Sega - 32X"],
    "sg1000": ["Sega - SG-1000"], "dreamcast": ["Sega - Dreamcast"], "saturn": ["Sega - Saturn"],
    "psx": ["Sony - PlayStation"], "psp": ["Sony - PlayStation Portable"],
    "pcengine": ["NEC - PC Engine - TurboGrafx 16"], "pcenginecd": ["NEC - PC Engine CD - TurboGrafx-CD"],
    "supergrafx": ["NEC - PC Engine SuperGrafx"], "pcfx": ["NEC - PC-FX"],
    "neogeocd": ["SNK - Neo Geo CD"], "ngp": ["SNK - Neo Geo Pocket", "SNK - Neo Geo Pocket Color"],
    "ngpc": ["SNK - Neo Geo Pocket Color", "SNK - Neo Geo Pocket"],
    "wswan": ["Bandai - WonderSwan", "Bandai - WonderSwan Color"],
    "wswanc": ["Bandai - WonderSwan Color", "Bandai - WonderSwan"],
    "lynx": ["Atari - Lynx"], "atari2600": ["Atari - 2600"], "atari5200": ["Atari - 5200"],
    "atari7800": ["Atari - 7800"], "atari800": ["Atari - 8-bit"], "jaguar": ["Atari - Jaguar"],
    "atarist": ["Atari - ST"], "3do": ["The 3DO Company - 3DO"], "colecovision": ["Coleco - ColecoVision"],
    "intellivision": ["Mattel - Intellivision"], "vectrex": ["GCE - Vectrex"],
    "o2em": ["Magnavox - Odyssey2"], "videopacplus": ["Philips - Videopac+"],
    "channelf": ["Fairchild - Channel F"], "supervision": ["Watara - Supervision"],
    "megaduck": ["Welback - Mega Duck"], "c64": ["Commodore - 64"], "vic20": ["Commodore - VIC-20"],
    "amiga500": ["Commodore - Amiga"], "amigacd32": ["Commodore - CD32"], "amstradcpc": ["Amstrad - CPC"],
    "zxspectrum": ["Sinclair - ZX Spectrum"], "zx81": ["Sinclair - ZX 81"],
    "msx1": ["Microsoft - MSX", "Microsoft - MSX2"], "msx2": ["Microsoft - MSX2", "Microsoft - MSX"],
    "x68000": ["Sharp - X68000"], "uzebox": ["Uzebox"], "tic80": ["TIC-80"], "pico8": ["PICO-8"],
    "scummvm": ["ScummVM"], "dos": ["DOS"],
    # arcade sets are named by their short names; the full name comes from Knulli's arcade list
    "mame": ["MAME", "FBNeo - Arcade Games"], "fbneo": ["FBNeo - Arcade Games", "MAME"],
    "neogeo": ["SNK - Neo Geo", "FBNeo - Arcade Games", "MAME"],
    "cps1": ["FBNeo - Arcade Games", "MAME"], "cps2": ["FBNeo - Arcade Games", "MAME"],
    "cps3": ["FBNeo - Arcade Games", "MAME"],
}
ARCADE = {"mame", "fbneo", "neogeo", "cps1", "cps2", "cps3"}
# TheGamesDB platform numbers, for the fallback search
TGDB = {"gb": 4, "gbc": 41, "sgb": 4, "gba": 5, "nds": 8, "nes": 7, "fds": 4936, "snes": 6, "virtualboy": 4918,
        "n64": 3, "megadrive": 18, "mastersystem": 35, "gamegear": 20, "segacd": 21, "sega32x": 33,
        "dreamcast": 16, "saturn": 17, "psx": 10, "psp": 13, "pcengine": 34, "pcfx": 4930, "neogeocd": 4956,
        "ngp": 4922, "ngpc": 4923, "wswan": 4925, "wswanc": 4926, "lynx": 4924, "atari2600": 22,
        "atari5200": 26, "atari7800": 27, "jaguar": 28, "colecovision": 31, "intellivision": 32,
        "vectrex": 4939, "c64": 40, "amiga500": 4911, "amstradcpc": 4914, "zxspectrum": 4913, "msx1": 4929,
        "3do": 25}

# GarlicOS's folder (the tablet's card, as the handheld's first card) -> the same
# collections; the same list as the PC kit's Get box art (pc/tools/get-game-art.ps1)
GARLIC = {
    "GB": ["Nintendo - Game Boy"], "GBC": ["Nintendo - Game Boy Color"],
    "SGB": ["Nintendo - Game Boy", "Nintendo - Game Boy Color"],
    "GBA": ["Nintendo - Game Boy Advance"], "GPSP": ["Nintendo - Game Boy Advance"],
    "FC": ["Nintendo - Nintendo Entertainment System"], "NESTOPIA": ["Nintendo - Nintendo Entertainment System"],
    "FDS": ["Nintendo - Family Computer Disk System"], "SFC": ["Nintendo - Super Nintendo Entertainment System"],
    "SUFAMI": ["Nintendo - Sufami Turbo"], "SATELLAVIEW": ["Nintendo - Satellaview"], "VB": ["Nintendo - Virtual Boy"],
    "POKE": ["Nintendo - Pokemon Mini"],
    "MD": ["Sega - Mega Drive - Genesis"], "MS": ["Sega - Master System - Mark III"], "GG": ["Sega - Game Gear"],
    "SEGACD": ["Sega - Mega-CD - Sega CD"], "THIRTYTWOX": ["Sega - 32X"], "SEGASGONE": ["Sega - SG-1000"],
    "PS": ["Sony - PlayStation"],
    "PCE": ["NEC - PC Engine - TurboGrafx 16"], "PCECD": ["NEC - PC Engine CD - TurboGrafx-CD"],
    "SGFX": ["NEC - PC Engine SuperGrafx"], "PCFX": ["NEC - PC-FX"],
    "NEOCD": ["SNK - Neo Geo CD"], "NGP": ["SNK - Neo Geo Pocket", "SNK - Neo Geo Pocket Color"],
    "WS": ["Bandai - WonderSwan", "Bandai - WonderSwan Color"],
    "LYNX": ["Atari - Lynx"], "ATARI": ["Atari - 2600"], "FIFTYTWOHUNDRED": ["Atari - 5200"],
    "SEVENTYEIGHTHUNDRED": ["Atari - 7800"], "JAGUAR": ["Atari - Jaguar"], "ATARIST": ["Atari - ST"],
    "PANASONIC": ["The 3DO Company - 3DO"], "COLECO": ["Coleco - ColecoVision"], "INTELLIVISION": ["Mattel - Intellivision"],
    "VECTREX": ["GCE - Vectrex"], "ODYSSEY": ["Magnavox - Odyssey2"], "VIDEOPAC": ["Magnavox - Odyssey2"],
    "FAIRCHILD": ["Fairchild - Channel F"], "SUPERVISION": ["Watara - Supervision"], "MEGADUCK": ["Welback - Mega Duck"],
    "COMMODORE": ["Commodore - 64"], "AMIGA": ["Commodore - Amiga"], "AMIGACD": ["Commodore - CD32"],
    "CPC": ["Amstrad - CPC"], "ZXS": ["Sinclair - ZX Spectrum"], "MSX": ["Microsoft - MSX", "Microsoft - MSX2"],
    "X68000": ["Sharp - X68000"],
}
GARLIC_TGDB = {"GB": 4, "GBC": 41, "GBA": 5, "GPSP": 5, "SGB": 4, "FC": 7, "NESTOPIA": 7, "FDS": 4936, "SFC": 6,
               "VB": 4918, "MD": 18, "MS": 35, "GG": 20, "SEGACD": 21, "THIRTYTWOX": 33, "PS": 10, "PCE": 34,
               "NGP": 4922, "WS": 4925, "LYNX": 4924, "ATARI": 22, "SEVENTYEIGHTHUNDRED": 27, "JAGUAR": 28,
               "COLECO": 31, "INTELLIVISION": 32, "VECTREX": 4939, "COMMODORE": 40, "AMIGA": 4911, "CPC": 4914,
               "ZXS": 4913, "MSX": 4929, "PANASONIC": 25, "NEOCD": 4956, "PCFX": 4930}
GARLIC_LAYOUT = False  # --layout garlic: Roms/<CONSOLE>/Imgs/<game>.png

# not games: saves, pictures, notes, the second half of a CD image ...
SKIP_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".txt", ".nfo", ".pdf", ".srm", ".sav", ".state", ".cfg", ".xml",
            ".dat", ".ips", ".bps", ".ups", ".json", ".db", ".ini", ".log", ".md", ".htm", ".html", ".mp4",
            ".mkv", ".avi", ".bak", ".part"}
SKIP_DIRS = {"images", "imgs", "videos", "manuals", "media", "downloaded_images", "downloaded_videos", "bios", "saves"}
SUFFIXES = ["-image", "-thumb", "-boxart", ""]  # the pictures Zappo X shows, in its order

STOP = threading.Event()
LOCK = threading.Lock()


# ------------------------------------------------------------------ names

def plain(t):
    """lower case, accents off: "Pokémon" -> "pokemon\""""
    d = unicodedata.normalize("NFD", t.lower())
    return "".join(c for c in d if unicodedata.category(c) != "Mn")


def key(name):
    """"Legend of Zelda, The - A Link to the Past (USA) (Rev 1)" -> "legendofzeldaalinktothepast\""""
    t = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", name)
    t = plain(t.replace("&", " and "))
    return "".join(w for w in re.split(r"[^\w]+|_", t) if w and w != "the")


def region(name):
    tags = ",".join(re.findall(r"[\(\[]([^\)\]]*)[\)\]]", name))
    if re.search(r"\bUSA\b|^U$|\bU\b", tags):
        return "usa"
    if re.search(r"\bWorld\b", tags):
        return "world"
    if re.search(r"\bEurope\b|\bUK\b|^E$|\bE\b", tags):
        return "eur"
    if re.search(r"\bJapan\b|^J$|\bJ\b", tags):
        return "jpn"
    return ""


def badness(cand, want):
    """lower is better"""
    b = 0
    if re.search(r"(?i)\((Beta|Proto|Demo|Sample|Kiosk|Pirate|Unl|Hack|Aftermarket)[^\)]*\)", cand):
        b += 20
    r = region(cand)
    if want and r == want:
        pass
    elif want and r == "world":
        b += 1
    else:
        b += {"usa": 2, "world": 2, "eur": 3, "jpn": 6, "": 5}[r]
    if "(Rev" in cand:
        b += 1
    return b


def safe(n):
    """the name libretro uses for a file name: these characters become "_\""""
    return re.sub(r'[&*/:`<>?\\|"]', "_", n)


def search_name(stem):
    """"PokemonQuetzalAlpha6v9" -> "Pokemon Quetzal Alpha", for TheGamesDB"""
    t = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", " ", stem)
    t = t.replace("_", " ").replace("-", " ")
    t = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", t)
    t = re.sub(r"(?i)\b(v|ver|version)?\d+(\.\d+)*[a-z]?\b", " ", t)
    t = re.sub(r"(?i)\b(alpha|beta|final|demo|fix|fixed|patched|rom|hack|gba|english|eng)\b", " ", t)
    return re.sub(r"\s+", " ", t).strip()


_arcade = None
_arcade_lock = threading.Lock()


def arcade_name(stem):
    """"sf2" -> "Street Fighter II: The World Warrior (World 910522)", from Knulli's arcade list"""
    global _arcade
    with _arcade_lock:  # (the download threads all ask at once)
        if _arcade is None:
            names = {}
            try:
                with open(ARCADE_NAMES, encoding="utf-8", errors="replace") as f:
                    for m in re.finditer(r'<rom id="([^"]+)"[^>]*?\bname="([^"]+)"', f.read()):
                        names[m.group(1)] = html.unescape(m.group(2))
            except OSError:
                pass
            _arcade = names
    return _arcade.get(stem)


# --------------------------------------------------------------- fetching

class Library:
    """the names in one libretro collection, and the pictures behind them"""

    def __init__(self, source, cache, log):
        self.source, self.cache, self.log = source.rstrip("/"), cache, log
        self.lists = {}
        self.local = not re.match(r"^https?://", self.source)
        self.offline = False  # the library couldn't be reached at all
        self.lock = threading.Lock()

    def url(self, sys_name, name=None):
        u = self.source + "/" + urllib.parse.quote(sys_name) + "/Named_Boxarts/"
        return u + urllib.parse.quote(name + ".png") if name is not None else u

    def fetch(self, url, timeout=60):
        if self.local:  # a folder laid out like the library (tests)
            p = os.path.join(self.source, *[urllib.parse.unquote(x) for x in url[len(self.source):].strip("/").split("/")])
            if os.path.isdir(p):
                return "".join('<a href="%s">' % urllib.parse.quote(n) for n in sorted(os.listdir(p))).encode()
            with open(p, "rb") as f:
                return f.read()
        req = urllib.request.Request(url, headers={"User-Agent": AGENT})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()

    def names(self, sys_name):
        """None when the list couldn't be read"""
        with self.lock:
            return self._names(sys_name)

    def _names(self, sys_name):
        if sys_name in self.lists:
            return self.lists[sys_name]
        os.makedirs(self.cache, exist_ok=True)
        cf = os.path.join(self.cache, safe(sys_name) + ".txt")
        names = None
        if not self.local and os.path.exists(cf) and time.time() - os.path.getmtime(cf) < 14 * 86400:
            with open(cf, encoding="utf-8") as f:
                names = [l.rstrip("\n") for l in f if l.strip()]
        if not names:
            try:
                page = self.fetch(self.url(sys_name)).decode("utf-8", "replace")
                names = [urllib.parse.unquote(m) for m in re.findall(r'href="([^"?/]+)\.png"', page)]
                if names and not self.local:
                    with open(cf + ".part", "w", encoding="utf-8") as f:
                        f.write("\n".join(names) + "\n")
                    os.replace(cf + ".part", cf)
            except Exception as e:
                self.log("could not read the list of %s pictures (%s)" % (sys_name, e))
                if not isinstance(e, (urllib.error.HTTPError, FileNotFoundError)):
                    self.offline = True
                names = None
        lib = None
        if names is not None:
            by_key, exact = {}, {}
            for n in names:
                exact.setdefault(n.lower(), n)
                k = key(n)
                if k:
                    by_key.setdefault(k, []).append(n)
            lib = {"by_key": by_key, "exact": exact, "count": len(names)}
        self.lists[sys_name] = lib
        return lib

    def find(self, collections, stem, full=None):
        """(collection, picture name) for a game, or None"""
        want = region(full or stem)
        best, best_sys, best_bad = None, None, 1e9
        for look in ([full] if full else []) + [stem]:
            k = key(look)
            for sys_name in collections:
                lib = self.names(sys_name)
                if not lib:
                    continue
                e = safe(look).lower()
                if e in lib["exact"]:
                    return sys_name, lib["exact"][e]
                for c in lib["by_key"].get(k, []) if k else []:
                    b = badness(c, want)
                    if b < best_bad:
                        best, best_sys, best_bad = c, sys_name, b
            if best:
                return best_sys, best
        return None


def tgdb_find(platform, stem):
    """the cover's address on TheGamesDB, or None"""
    q = search_name(stem)
    words = [w for w in re.split(r"[^a-z0-9]+", plain(q)) if len(w) > 2]
    if not words:
        return None
    url = "https://thegamesdb.net/search.php?name=" + urllib.parse.quote(q) + "&platform_id%5B%5D=" + str(platform)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": AGENT})
        with urllib.request.urlopen(req, timeout=40) as r:
            page = r.read().decode("utf-8", "replace")
    except Exception:
        return None
    seen, cands = set(), []
    for m in re.finditer(r"game\.php\?id=(\d+)", page):
        gid = m.group(1)
        if gid in seen:
            continue
        seen.add(gid)
        # the words of the name must appear near the link
        near = plain(html.unescape(re.sub(r"<[^>]+>", " ", page[m.start():m.start() + 700])))
        if all(w in near for w in words):
            cands.append(gid)
    out = []
    for gid in cands[:4]:
        for ext in ("jpg", "png"):
            out.append("https://cdn.thegamesdb.net/images/thumb/boxart/front/%s-1.%s" % (gid, ext))
    return out or None


def save_small(data, dest, biggest):
    """the picture, at most biggest x biggest, as a PNG (written whole or not at all)"""
    part = os.path.join(os.path.dirname(dest), "." + os.path.basename(dest) + ".part")
    try:
        if Image is not None:
            import io
            im = Image.open(io.BytesIO(data))
            im.load()
            if im.mode not in ("RGB", "RGBA"):
                im = im.convert("RGBA" if "transparency" in im.info or im.mode in ("LA", "PA") else "RGB")
            im.thumbnail((biggest, biggest), Image.LANCZOS)
            im.save(part, "PNG")
        else:
            with open(part, "wb") as f:
                f.write(data)
        os.replace(part, dest)
        return True
    except Exception:
        try:
            os.remove(part)
        except OSError:
            pass
        return False


# ------------------------------------------------------------------ games

def es_systems(roms):
    """Knulli folder -> (name shown, folder path, extensions), from its es_systems files"""
    out = {}
    files = []
    for p in ES_SYSTEMS:
        files += sorted(glob.glob(p))
    for p in files:
        try:
            root = ET.parse(p).getroot()
        except (OSError, ET.ParseError):
            continue
        for s in root.iter("system"):
            name = (s.findtext("name") or "").strip()
            path = (s.findtext("path") or "").strip()
            exts = (s.findtext("extension") or "").lower().split()
            if not name or not path:
                continue
            folder = os.path.basename(path.rstrip("/"))
            out[folder] = ((s.findtext("fullname") or name).strip(), os.path.join(roms, folder), set(exts))
    return out


def pic_dir(folder):
    return os.path.join(folder, "Imgs" if GARLIC_LAYOUT else "images")


def pic_path(folder, stem):
    """where a new picture goes"""
    return os.path.join(pic_dir(folder), stem + (".png" if GARLIC_LAYOUT else "-image.png"))


def art_for(folder, stem):
    """the picture Zappo X would show for this game, if there is one"""
    if GARLIC_LAYOUT:
        for ext in ("png", "jpg"):
            p = os.path.join(folder, "Imgs", stem + "." + ext)
            if os.path.exists(p):
                return p
        return None
    for suf in SUFFIXES:
        for ext in ("png", "jpg"):
            p = os.path.join(folder, "images", stem + suf + "." + ext)
            if os.path.exists(p):
                return p
    return None


def games(folder, exts):
    """(relative path, stem) of every game under folder, without a picture yet"""
    todo, have = [], 0
    for dirpath, dirs, files in os.walk(folder):
        dirs[:] = sorted(d for d in dirs if not d.startswith(".") and d.lower() not in SKIP_DIRS)
        rel_dir = os.path.relpath(dirpath, folder)
        for fn in sorted(files):
            if fn.startswith("."):
                continue
            stem, ext = os.path.splitext(fn)
            ext = ext.lower()
            if exts and ext not in exts:
                continue
            if not exts and (ext in SKIP_EXT or not ext):
                continue
            rel = fn if rel_dir == "." else os.path.join(rel_dir, fn)
            if art_for(folder, stem):
                have += 1
            else:
                todo.append((rel, stem))
    return todo, have


def gamelist_add(folder, added, log):
    """puts the new pictures into Knulli's gamelist.xml (for its own menu)"""
    gl = os.path.join(folder, "gamelist.xml")
    try:
        if os.path.exists(gl):
            tree = ET.parse(gl)
            root = tree.getroot()
            if not os.path.exists(gl + ".before-zappo-x"):
                with open(gl, "rb") as a, open(gl + ".before-zappo-x", "wb") as b:
                    b.write(a.read())
        else:
            root = ET.Element("gameList")
            tree = ET.ElementTree(root)
        by_path = {}
        for g in root.findall("game"):
            p = (g.findtext("path") or "").strip()
            if p.startswith(folder + "/"):
                p = "./" + p[len(folder) + 1:]
            by_path[p] = g
        changed = 0
        for rel, stem, pic in added:
            p = "./" + rel.replace(os.sep, "/")
            g = by_path.get(p)
            if g is None:
                g = ET.SubElement(root, "game")  # (no name: Knulli's menu picks its usual one)
                ET.SubElement(g, "path").text = p
            if g.find("image") is None or not (g.findtext("image") or "").strip():
                if g.find("image") is None:
                    ET.SubElement(g, "image")
                g.find("image").text = "./images/" + os.path.basename(pic)
                changed += 1
        if changed:
            ET.indent(tree, "\t")
            tree.write(gl + ".part", encoding="utf-8", xml_declaration=True)
            with open(gl + ".part", "a", encoding="utf-8") as f:
                f.write("\n")
            os.replace(gl + ".part", gl)
    except Exception as e:
        log("could not update %s (%s)" % (gl, e))


# ------------------------------------------------------------------- main

class Status:
    def __init__(self, path):
        self.path = path
        self.v = {"state": "looking", "done": 0, "total": 0, "found": 0, "already": 0, "system": "", "game": "",
                  "error": "", "pid": os.getpid()}

    def set(self, **kw):
        with LOCK:
            self.v.update(kw)
            if not self.path:
                return
            with open(self.path + ".part", "w", encoding="utf-8") as f:
                for k, v in self.v.items():
                    f.write("%s=%s\n" % (k, str(v).replace("\n", " ")))
            os.replace(self.path + ".part", self.path)


def main():
    global ARCADE_NAMES, GARLIC_LAYOUT, LIBRETRO, TGDB, ARCADE
    ap = argparse.ArgumentParser(description="Box art for Zappo X from the libretro thumbnail library")
    ap.add_argument("--layout", choices=["knulli", "garlic"], default="knulli",
                    help="garlic: Roms/<CONSOLE>/Imgs/<game>.png (Zappo X on a tablet)")
    ap.add_argument("--roms", default="")
    ap.add_argument("--home", default=HOME, help="Zappo X's folder (logs, cache, box-art-source)")
    ap.add_argument("--systems", default="", help="console folders, comma separated (default: all)")
    ap.add_argument("--source", default="https://thumbnails.libretro.com")
    ap.add_argument("--status", default="")
    ap.add_argument("--log", default="")
    ap.add_argument("--cache", default="")
    ap.add_argument("--max", type=int, default=320, help="biggest picture side")
    ap.add_argument("--no-tgdb", action="store_true")
    ap.add_argument("--no-gamelist", action="store_true")
    ap.add_argument("--es-systems", default="", help="es_systems file to read instead of Knulli's")
    ap.add_argument("--arcade-names", default=ARCADE_NAMES, help="Knulli's arcade name list")
    a = ap.parse_args()
    if a.layout == "garlic":
        GARLIC_LAYOUT = True
        LIBRETRO, TGDB, ARCADE = GARLIC, GARLIC_TGDB, set()
        a.no_gamelist = True
        ES_SYSTEMS[:] = []
    a.roms = a.roms or ("Roms" if GARLIC_LAYOUT else "/userdata/roms")
    a.status = a.status or os.path.join(os.environ.get("TMPDIR", "/tmp"), "zappo-x-art.status")
    a.log = a.log or os.path.join(a.home, "logs", "box-art.txt")
    a.cache = a.cache or os.path.join(a.home, "cache", "box-art")
    # another copy of the library (a mirror, or a folder laid out the same way)
    # can be named in zappo-x/box-art-source
    try:
        with open(os.path.join(a.home, "box-art-source"), encoding="utf-8") as f:
            alt = f.readline().strip()
        if alt and a.source == ap.get_default("source"):
            a.source = alt
    except OSError:
        pass
    ARCADE_NAMES = a.arcade_names
    if a.es_systems:
        ES_SYSTEMS[:] = [a.es_systems]
    try:
        os.nice(15)
    except OSError:
        pass

    # Knulli's battery saver would put the handheld to sleep with nobody pressing buttons
    pause = None if GARLIC_LAYOUT else "/var/run/battery-saver/get-box-art.pause"
    try:
        if pause:
            os.makedirs(os.path.dirname(pause), exist_ok=True)
            open(pause, "w").close()
    except OSError:
        pause = None
    try:
        return run(a)
    finally:
        if pause:
            try:
                os.remove(pause)
            except OSError:
                pass


def run(a):
    st = Status(a.status)
    signal.signal(signal.SIGTERM, lambda *_: STOP.set())
    signal.signal(signal.SIGINT, lambda *_: STOP.set())
    os.makedirs(os.path.dirname(a.log), exist_ok=True)
    logf = open(a.log, "a", encoding="utf-8")
    logf.write("\n==== %s\n" % time.strftime("%Y-%m-%d %H:%M"))

    def log(m):
        with LOCK:
            logf.write(m + "\n")
            logf.flush()

    lib = Library(a.source, a.cache, log)
    known = es_systems(a.roms)
    want = [s.strip() for s in a.systems.split(",") if s.strip()]
    folders = want or sorted(d for d in os.listdir(a.roms) if os.path.isdir(os.path.join(a.roms, d)))

    # what needs a picture
    work, already = [], 0
    for d in folders:
        if d not in LIBRETRO:
            continue
        name, path, exts = known.get(d, (d, os.path.join(a.roms, d), set()))
        if not os.path.isdir(path):
            continue
        todo, have = games(path, exts)
        already += have
        if todo:
            work.append((d, name, path, todo))
    total = sum(len(w[3]) for w in work)
    if want and not any(d in LIBRETRO for d in folders):
        st.set(state="failed", error="The box-art library has no pictures for this console.")
        log("no pictures in the library for " + ", ".join(folders))
        return 0
    st.set(state="getting", total=total, already=already)
    if not total:
        st.set(state="finished")
        log("every game already has a picture (%d)" % already)
        return 0

    lists_ok = False
    found = done = 0
    use_tgdb = not a.no_tgdb and not lib.local
    for d, name, path, todo in work:
        if STOP.is_set():
            break
        st.set(system=name, game="")
        cols = LIBRETRO[d]
        if any(lib.names(c) for c in cols):
            lists_ok = True
        elif lib.offline:
            break
        elif not use_tgdb or d not in TGDB:
            done += len(todo)
            st.set(done=done)
            continue
        os.makedirs(pic_dir(path), exist_ok=True)
        added = []

        def one(rel, stem):
            if STOP.is_set():
                return None
            full = arcade_name(stem) if d in ARCADE else None
            dest = pic_path(path, stem)
            hit = lib.find(cols, stem, full)
            if hit:
                try:
                    data = lib.fetch(lib.url(*hit))
                    if len(data) > 200 and save_small(data, dest, a.max):
                        return rel, stem, dest, "%s/%s" % hit
                except Exception as e:
                    log("could not get %s/%s (%s)" % (hit[0], hit[1], e))
            if use_tgdb and d in TGDB and d not in ARCADE:
                for u in tgdb_find(TGDB[d], stem) or []:
                    try:
                        data = lib.fetch(u, 40)
                        if len(data) > 2000 and save_small(data, dest, a.max):
                            return rel, stem, dest, "TheGamesDB " + u.rsplit("/", 1)[1]
                    except Exception:
                        continue
            return rel, stem, None, None

        with ThreadPoolExecutor(max_workers=4) as pool:
            jobs = [pool.submit(one, rel, stem) for rel, stem in todo]
            for fut in as_completed(jobs):
                if STOP.is_set():
                    for j in jobs:
                        j.cancel()
                r = None
                try:
                    r = fut.result()
                except Exception as e:
                    log("error: %s" % e)
                done += 1
                if r and r[2]:
                    found += 1
                    added.append(r[:3])
                    log("found   %s/%s  <-  %s" % (d, r[0], r[3]))
                elif r:
                    log("missing %s/%s" % (d, r[0]))
                st.set(done=done, found=found, game=r[1] if r else "")
        if added and not a.no_gamelist:
            gamelist_add(path, added, log)

    if STOP.is_set():
        st.set(state="stopped", game="")
    elif lib.offline and not found:
        st.set(state="failed", game="", error="Couldn't reach the box-art library. Is Wi-Fi connected?")
    elif not lists_ok and not found:
        st.set(state="failed", game="", error="The box-art library has nothing for these consoles.")
    else:
        st.set(state="finished", game="", done=total)
    log("%d found, %d without a picture, %d had one already" % (found, done - found, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
