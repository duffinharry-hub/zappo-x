#!/bin/sh
# Builds the release zips in release/out:
#   Zappo-<ver>.zip          the download: Windows kit + card files + boot pictures + manual
#   Zappo-<ver>-source.zip   the source tree (git archive)
# Run from anywhere; needs python3. Rebuild the binaries first (make arm; make -C blockbox arm)
# and copy them into package/ if they changed.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
VER=${1:-0.3.5}
OUT=$R/release/out
STAGE=$OUT/Zappo-$VER
rm -rf "$STAGE"; mkdir -p "$STAGE/card" "$OUT"
# the card files: everything in package/
cp -r "$R/package/CFW" "$R/package/Roms" "$R/package/Import" "$R/package/Music" "$STAGE/card/"
cp "$R/package/README.txt" "$STAGE/card/Zappo README.txt"
cp "$R/package/README.txt" "$STAGE/Zappo manual.txt"
cp "$R/LICENSE" "$STAGE/LICENSE.txt"
cp -r "$R/pc/boot" "$R/pc/tools" "$STAGE/"
cp "$R/pc/Zappo.bat" "$R/pc/READ ME.txt" "$R/pc/zappo.sh" "$STAGE/"
# only Zappo's own files may be in here
if find "$STAGE/card" -name '*.so' | grep -v blockbox_libretro.so; then echo "a core that is not Zappo's is in the card folder"; exit 1; fi
if [ -d "$STAGE/card/BIOS" ] || [ -f "$STAGE/card/CFW/main" ] || [ -f "$STAGE/card/CFW/rootfs.img" ]; then echo "GarlicOS or BIOS files in the card folder"; exit 1; fi
find "$STAGE" -name '.DS_Store' -o -name 'Thumbs.db' -o -name '__pycache__' | xargs rm -rf
# Windows line endings for the text the kit's users open
find "$STAGE" \( -name '*.ps1' -o -name '*.bat' -o -name '*.txt' -o -name '*.md' \) -exec sed -i 's/\r$//; s/$/\r/' {} +
# the handheld's own scripts stay LF
find "$STAGE/card" \( -name '*.sh' -o -name 'main.sh' \) -exec sed -i 's/\r$//' {} +
sed -i 's/\r$//' "$STAGE/zappo.sh"; chmod +x "$STAGE/zappo.sh"
( cd "$STAGE/card" && find . -type f ! -name 'manifest.md5' -exec md5sum {} + | sed 's|  \./|  |' | sort -k2 ) > "$STAGE/tools/card-manifest.md5"
python3 - "$OUT/Zappo-$VER.zip" "$STAGE" <<'PY'
import os, sys, zipfile
out, stage = sys.argv[1], sys.argv[2]
top = os.path.basename(stage)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, dirs, files in os.walk(stage):
        dirs.sort()
        rel = os.path.relpath(root, stage)
        if not files and not dirs:
            z.writestr(zipfile.ZipInfo(top + '/' + rel + '/'), b'')   # keep empty folders (Import, Music, Roms/VIDEOS)
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, top + '/' + os.path.normpath(os.path.join(rel, f)))
print(out, os.path.getsize(out) // 1024, 'KB')
PY
if [ -d "$R/.git" ]; then (cd "$R" && git archive --format=zip --prefix="Zappo-$VER-source/" -o "$OUT/Zappo-$VER-source.zip" HEAD) && echo "$OUT/Zappo-$VER-source.zip"; fi
