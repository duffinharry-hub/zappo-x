#!/bin/sh
# Makes the Zappo X kit for the PC: build/Zappo-X-Kit.zip
#   Zappo X Kit/
#     Zappo X.bat, zappo-x-kit.sh, READ ME.txt, LICENSE, THIRD-PARTY.md
#     tools/        the kit's PowerShell scripts
#     card/         postshare.sh + zappo-x-kit/install (tools/knulli-package.sh)
#     test-pack/    tools/make-test-pack.sh
set -e
cd "$(dirname "$0")/.."
make -s knulli
sh tools/knulli-package.sh >/dev/null
P=build/knulli-pkg
K="build/zappo-x-kit/Zappo X Kit"
rm -rf build/zappo-x-kit && mkdir -p "$K/tools" "$K/card/zappo-x-kit"
# Windows files with Windows line ends; what runs on the handheld keeps Unix ones
for f in "Zappo X.bat" "READ ME.txt"; do sed 's/$/\r/' "pc-x/$f" > "$K/$f"; done
for f in pc-x/tools/*.ps1; do sed 's/$/\r/' "$f" > "$K/tools/$(basename "$f")"; done
cp pc-x/zappo-x-kit.sh LICENSE THIRD-PARTY.md "$K/"
cp knulli/postshare.sh "$K/card/postshare.sh"
cp knulli/survey-tf2.sh "$K/card/zappo-x-kit/survey-tf2.sh"   # one read-only look at TF2 (stock card games)
cp -a $P "$K/card/zappo-x-kit/install"
sh tools/make-test-pack.sh "$K/test-pack" >/dev/null
rm -f build/Zappo-X-Kit.zip
(cd build/zappo-x-kit && zip -qr -X ../Zappo-X-Kit.zip "Zappo X Kit")
du -sh "$K" build/Zappo-X-Kit.zip
