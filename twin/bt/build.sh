#!/bin/sh
# Builds zappo-twin-bt (the twin's Bluetooth stand-in) for the twin, from
# zappo-twin-bt.c and BlueZ 5.79's controller emulator and shared code.
# Needs: BlueZ 5.79 source in $SRC/bluez, the sysroot (gpu/mksysroot.py), clang/lld.
# Output: $OUT/usr/bin/zappo-twin-bt (laid over Knulli at start-up like the rest of twin/root).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TWIN=${TWIN:-/home/claude/twin}
SRC=${SRC:-$TWIN/src}
S=${SYSROOT:-$TWIN/sysroot}
OUT=${OUT:-$TWIN/bt-stage}
B=${B:-$TWIN/build/bt}
BZ=$SRC/bluez
export PATH=$TWIN/tools/bin:$PATH
mkdir -p "$B" "$OUT/usr/bin"
printf '#define VERSION "5.79"\n#define PACKAGE "bluez"\n#define HAVE_CONFIG_H 1\n' > "$B/config.h"
# src/shared/ad.c includes src/eir.h, which wants glib only for a list type
mkdir -p "$B/bluetooth" && for h in "$BZ"/lib/*.h; do ln -sf "$h" "$B/bluetooth/"; done   # <bluetooth/...> as BlueZ's build lays it out
printf '/* stand-in: just what src/eir.h needs */\ntypedef struct _GSList GSList;\n' > "$B/glib.h"
clang --target=aarch64-linux-gnu --sysroot="$S" -fuse-ld=lld -O2 -g0 -std=gnu11 \
  -Wno-unused-parameter -Wno-sign-compare -Wno-address-of-packed-member -Wno-format-truncation \
  -I"$B" -I"$BZ" -include "$B/config.h" \
  -o "$OUT/usr/bin/zappo-twin-bt" \
  "$HERE/zappo-twin-bt.c" \
  "$BZ/emulator/vhci.c" "$BZ/emulator/btdev.c" "$BZ/emulator/bthost.c" "$BZ/emulator/smp.c" \
  "$BZ/src/shared/mainloop.c" "$BZ/src/shared/mainloop-notify.c" "$BZ/src/shared/io-mainloop.c" \
  "$BZ/src/shared/timeout-mainloop.c" "$BZ/src/shared/queue.c" "$BZ/src/shared/util.c" \
  "$BZ/src/shared/crypto.c" "$BZ/src/shared/ecc.c" "$BZ/src/shared/ad.c" "$BZ/lib/uuid.c" \
  "$BZ/lib/bluetooth.c" "$BZ/lib/hci.c"
llvm-strip --strip-unneeded "$OUT/usr/bin/zappo-twin-bt"
ls -l "$OUT/usr/bin/zappo-twin-bt"
