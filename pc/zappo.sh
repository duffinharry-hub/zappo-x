#!/bin/sh
# Zappo - installer for Linux and macOS (the Windows kit is Zappo.bat).
# Needs: sh, python3 (for the video player fix and the charging animation), md5sum or md5.
#
#   ./zappo.sh install [CARD] [BOOT]   put Zappo on a GarlicOS card (safe to run again)
#   ./zappo.sh remove  [CARD] [BOOT]   take it off: GarlicOS's menu and video player back
#   ./zappo.sh player  [CARD]          video player fix only      (player undo: undoes it)
#   ./zappo.sh boot    [BOOT]          boot logo + charging animation only  (boot undo)
#
# CARD is where the card's ROMS partition is mounted (the one with CFW, BIOS and Roms),
# BOOT the small boot partition (dmenu.bin, ramdisk.img). Left out, they are looked
# for under /media, /run/media, /mnt and /Volumes. Nothing of yours is deleted:
# what is replaced is kept in backups/ next to this script.
set -eu
KIT=$(cd "$(dirname "$0")" && pwd)
CMD=${1:-help}; shift || true
UNDO=0; [ "${1:-}" = undo ] && { UNDO=1; shift; }
BK="$KIT/backups"; mkdir -p "$BK"
LOG="$KIT/logs/zappo-sh-log.txt"; mkdir -p "$KIT/logs"
say() { printf '  %s\n' "$*"; printf '%s\n' "$*" >> "$LOG"; }
die() { say "ERROR: $*"; exit 1; }
md5() { if command -v md5sum >/dev/null 2>&1; then md5sum "$1" | cut -c1-32; else md5 -q "$1"; fi; }
have_py() { command -v python3 >/dev/null 2>&1; }
copyv() { # copy, then check
	mkdir -p "$(dirname "$2")"; cp -f "$1" "$2.part"
	[ "$(md5 "$1")" = "$(md5 "$2.part")" ] || { rm -f "$2.part"; die "could not copy $1 to $2"; }
	mv -f "$2.part" "$2"
}
find_card() {
	if [ -n "${1:-}" ]; then [ -d "$1/CFW" ] || die "$1 has no CFW folder, so it is not a GarlicOS card"; CARD=${1%/}; return; fi
	set -- $(for d in /media/*/* /media/* /run/media/*/* /mnt/* /Volumes/*; do [ -d "$d/CFW/retroarch" ] && [ -d "$d/Roms" ] && printf '%s\n' "$d"; done 2>/dev/null)
	[ $# -eq 1 ] || { [ $# -eq 0 ] && die 'No GarlicOS card found. Mount the card and give its path: ./zappo.sh install /media/you/ROMS'; die "More than one GarlicOS card is mounted ($*). Give the one you mean."; }
	CARD=${1%/}
}
find_boot() {
	BOOT=''
	if [ -n "${1:-}" ]; then [ -f "$1/ramdisk.img" ] || die "$1 has no ramdisk.img"; BOOT=${1%/}; return; fi
	for d in /media/*/* /media/* /run/media/*/* /mnt/* /Volumes/*; do [ -f "$d/dmenu.bin" ] && [ -f "$d/ramdisk.img" ] && { BOOT=${d%/}; return; }; done 2>/dev/null
	return 0
}
printf '\n== zappo.sh %s %s - %s\n' "$CMD" "$([ $UNDO = 1 ] && echo undo)" "$(date)" >> "$LOG"

# ---------- video player: 47 bytes in GarlicOS's own ffmpeg core (see tools/player-fix.ps1)
player() {
	CORE="$CARD/CFW/retroarch/.retroarch/cores/ffmpeg_libretro.so"
	[ -f "$CORE" ] || { say 'No video player core on the card (CFW/retroarch/.retroarch/cores/ffmpeg_libretro.so).'; return 1; }
	have_py || { say 'python3 is needed for the video player fix; skipped.'; return 1; }
	python3 - "$CORE" "$1" <<'PY'
import hashlib, os, shutil, sys
core, mode = sys.argv[1], sys.argv[2]
STOCK = '4104da876f4b459fead4dd16305c0e47'; FIXED = '7a4fc34e20db1fcebfef24cd073d38d3'; OFF = 2825541
OLD = bytes.fromhex('109de5040090e5010851e30d00000a00108de56d24f7ebc01fa0e1000050e30030d1e2010000ba64d08de204f09de4')
NEW = bytes.fromhex('c09de5040090e501085ce30d00000a0210a0e10c20a0e16c24f7ebc01fb0e10200004a64d08de204f09de40000a0e1')
def md5(p): return hashlib.md5(open(p, 'rb').read()).hexdigest()
def write(b, want):
    open(core + '.part', 'wb').write(b)
    if md5(core + '.part') != want: os.remove(core + '.part'); sys.exit('  the result was not the expected file - nothing changed')
    os.replace(core + '.part', core)
have = md5(core); keep = core + '.garlic'
if mode == 'undo':
    if have == STOCK: print("  The video player is already GarlicOS's original."); sys.exit(0)
    if os.path.exists(keep) and md5(keep) == STOCK: shutil.copyfile(keep, core); print("  GarlicOS's original video player is back."); sys.exit(0)
    if have == FIXED:
        b = bytearray(open(core, 'rb').read()); b[OFF:OFF + len(OLD)] = OLD; write(bytes(b), STOCK); print("  GarlicOS's original video player is back."); sys.exit(0)
    sys.exit('  This video player is not one this kit knows, so it was left alone.')
if have == FIXED: print('  The video player is already fixed.'); sys.exit(0)
if have != STOCK: sys.exit('  This video player is not the GarlicOS build the fix was made for, so it was left alone (skipping may close it).')
if not os.path.exists(keep): shutil.copyfile(core, keep)
b = bytearray(open(core, 'rb').read())
if bytes(b[OFF:OFF + len(OLD)]) != OLD: sys.exit('  the bytes to change are not where they should be - nothing changed')
b[OFF:OFF + len(NEW)] = NEW; write(bytes(b), FIXED)
print('  Video player fixed (skipping works; B leaves cleanly). The original is kept beside it as ffmpeg_libretro.so.garlic.')
PY
}

# ---------- boot partition: Zappo's pictures, and the charging animation inside ramdisk.img
PICS='boot_logo.bmp.gz charger_logo.bmp.gz battery_low.bmp.gz 0_charger_frame.bmp.gz 1_charger_frame.bmp.gz 2_charger_frame.bmp.gz 3_charger_frame.bmp.gz 4_charger_frame.bmp.gz 5_charger_frame.bmp.gz'
boot() {
	B="$BK/boot-partition"; mkdir -p "$B"
	if [ "$1" = undo ]; then
		n=0; for f in $PICS ramdisk.img; do [ -f "$B/$f" ] && { copyv "$B/$f" "$BOOT/$f"; n=$((n+1)); }; done
		[ $n -gt 0 ] && say "GarlicOS's own boot pictures are back ($n files)." || say 'Nothing to put back: this kit never changed the boot partition.'
		return 0
	fi
	for f in $PICS ramdisk.img; do [ -f "$BOOT/$f" ] && [ ! -f "$B/$f" ] && copyv "$BOOT/$f" "$B/$f"; done
	for f in $PICS; do copyv "$KIT/boot/$f" "$BOOT/$f"; done
	say 'Boot logo, charging screen and battery frames updated.'
	[ -f "$BOOT/ramdisk.img" ] || { say 'No ramdisk.img on the boot partition; the charging animation was left alone.'; return 0; }
	have_py || { say 'python3 is needed for the charging animation; skipped.'; return 0; }
	python3 - "$B/ramdisk.img" "$BOOT/ramdisk.img" "$KIT/boot/charger" "$B/ramdisk.known" <<'PY'
import hashlib, os, struct, sys, zlib
orig, cur, pngdir, knownf = sys.argv[1:5]
KNOWN = {'e539d2e3f6583382e6a6244ada8fb4bf',            # stock GarlicOS 1.3 / 1.4 (by contents)
         'a4f0e52af651de3db4a2e06a8fa26594',            # earlier Zappo charging pictures
         '216881482dbfcbbd670d62f4ff89eee3',
         'd67a5618aa0d17f76cd9d3fc60466354',
         'a0760fdf7755836e3c6406be057029b7',
         '0093a5f15f461b704b405fad5e52493a',             # drawn Zappo and fire Zappo
         'f9b3d845c6dacef9331513e9aeb40c2f'}            # Zappo's current charging pictures (the animated scene, battery filling)
if os.path.exists(knownf): KNOWN |= {l.strip() for l in open(knownf) if l.strip()}
REPL = {'battery_%d' % i for i in range(6)} | {'battery_charge', 'battery_fail'}
def walk(d):
    body = d[64:64 + struct.unpack('>I', d[12:16])[0]]; i = 0
    while i + 110 <= len(body):
        h = body[i:i + 110]
        if h[:6] != b'070701': raise SystemExit('  not a cpio ramdisk - left alone')
        ns = int(h[94:102], 16); fs = int(h[54:62], 16)
        name = body[i + 110:i + 110 + ns - 1]; start = (i + 110 + ns + 3) & ~3
        yield h, name, body[start:start + fs]
        if name == b'TRAILER!!!': return
        i = (start + fs + 3) & ~3
def fp(d):
    m = hashlib.md5()
    for h, name, data in walk(d):
        if name == b'TRAILER!!!': break
        m.update(name); m.update(b'\0'); m.update(data)
    return m.hexdigest()
d = open(cur, 'rb').read()
if d[:4] != b'\x27\x05\x19\x56': raise SystemExit('  ramdisk.img is not a u-boot image - left alone')
have = fp(d)
if have not in KNOWN: raise SystemExit('  ramdisk.img is not the GarlicOS one this kit knows, so the charging animation was left alone (the pictures above still apply).')
o = open(orig, 'rb').read(); out = bytearray(); done = 0
for h, name, data in walk(o):
    stem = os.path.splitext(name.decode())[0]
    if name.startswith(b'res/images/charger/') and os.path.basename(stem) in REPL:
        p = os.path.join(pngdir, os.path.basename(stem) + '.png')
        if os.path.exists(p): data = open(p, 'rb').read(); done += 1
    h = bytearray(h); h[54:62] = b'%08X' % len(data)
    out += h + name + b'\0'; out += b'\0' * (-len(out) % 4); out += data; out += b'\0' * (-len(out) % 4)
    if name == b'TRAILER!!!': break
if done != 8: raise SystemExit('  only %d of the 8 charger pictures were found in the ramdisk - not touching it' % done)
out += b'\0' * (-len(out) % 512)
hdr = bytearray(o[:64]); hdr[12:16] = struct.pack('>I', len(out)); hdr[24:28] = struct.pack('>I', zlib.crc32(out) & 0xffffffff)
hdr[4:8] = b'\0\0\0\0'; hdr[4:8] = struct.pack('>I', zlib.crc32(hdr) & 0xffffffff)
new = bytes(hdr) + bytes(out)
if fp(new) == have: print('  Charging animation already in place.'); sys.exit(0)
st = os.statvfs(os.path.dirname(cur)); extra = len(new) - os.path.getsize(cur)
if extra > 0 and st.f_bavail * st.f_frsize < extra + 262144: raise SystemExit('  Not enough room on the boot partition for the charging animation; skipped.')
open(cur + '.part', 'wb').write(new); os.replace(cur + '.part', cur)
open(knownf, 'a').write(fp(new) + '\n')
print('  Charging animation updated (ramdisk.img rewritten; the original is in backups/boot-partition).')
PY
}

case "$CMD" in
install)
	find_card "${1:-}"; find_boot "${2:-}"
	SRC="$KIT/card"; [ -f "$SRC/CFW/clove/clove" ] || die 'this kit is missing its card folder - unzip Zappo again'
	copied=0; same=0
	LIST="$KIT/logs/.files"; ( cd "$SRC" && find . -type f ) > "$LIST"
	while IFS= read -r rel; do
		rel=${rel#./}; dst="$CARD/$rel"
		if [ -f "$dst" ] && [ "$(md5 "$dst")" = "$(md5 "$SRC/$rel")" ]; then same=$((same+1)); continue; fi
		copyv "$SRC/$rel" "$dst"; copied=$((copied+1))
	done < "$LIST"; rm -f "$LIST"
	say "Zappo files on the card: $copied copied, $same already up to date."
	if [ -f "$CARD/CFW/main" ] && grep -q CLOVE_WRAPPER "$CARD/CFW/main"; then copyv "$SRC/CFW/clove/main.sh" "$CARD/CFW/main"; fi
	chmod 755 "$CARD/CFW/clove/clove" "$CARD/CFW/clove/main.sh" "$CARD/CFW/autostart/clove-install.sh" 2>/dev/null || true
	for d in Import Music Roms/VIDEOS Saves Screenshots; do mkdir -p "$CARD/$d"; done
	player apply || true
	if [ -n "$BOOT" ]; then boot apply; else say 'Boot partition not mounted, so the boot logo and charging animation were skipped (./zappo.sh boot <path> when it is).'; fi
	sync
	say 'DONE. Eject the card, put it in the handheld and switch on: the first start finishes the install and opens Zappo.'
	;;
remove)
	find_card "${1:-}"; find_boot "${2:-}"
	player undo || true
	if [ -f "$CARD/CFW/main.garlic" ] && grep -q CLOVE_WRAPPER "$CARD/CFW/main" 2>/dev/null; then
		copyv "$CARD/CFW/main.garlic" "$CARD/CFW/main"; rm -f "$CARD/CFW/main.garlic"; say "GarlicOS's own menu is back (CFW/main)."
	fi
	rm -f "$CARD/CFW/autostart/clove-install.sh"
	R="$BK/removed-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$R"
	[ -d "$CARD/CFW/clove" ] && mv "$CARD/CFW/clove" "$R/clove"
	[ -f "$CARD/Roms/APPS/Switch to Clove.sh" ] && mv "$CARD/Roms/APPS/Switch to Clove.sh" "$R/"
	say "Zappo's own folder (CFW/clove: settings, playlists, themes) was moved to $R, not deleted. Games, music, films and saves are untouched."
	[ -n "$BOOT" ] && boot undo
	sync; say 'DONE - Zappo removed.'
	;;
player) find_card "${1:-}"; if [ $UNDO = 1 ]; then player undo; else player apply; fi ;;
boot)   find_boot "${1:-}"; [ -n "$BOOT" ] || die 'The boot partition is not mounted (the small one with dmenu.bin and ramdisk.img). Give its path: ./zappo.sh boot /media/you/boot'; if [ $UNDO = 1 ]; then boot undo; else boot apply; fi ;;
*) sed -n '2,15p' "$0" ;;
esac
