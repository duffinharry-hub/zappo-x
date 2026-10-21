#!/bin/sh
# Zappo X device twin - one-time setup on Ubuntu (22.04 or newer, x86-64).
#   sh setup.sh /path/to/knulli-h700-rg40xx-h-gladiator-ii-20251230.img
# Unpacks the twin's QEMU, makes the 8 GB card image from Knulli's image, and
# leaves everything in this folder. Then: sh start.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
IMG=$1
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: sh setup.sh KNULLI.img   (unpack the .7z parts from E:\\Zappo\\Zappo X\\Knulli first: 7z x knulli-...7z.001)"; exit 1; }
command -v python3 >/dev/null || sudo apt-get install -y python3
command -v sgdisk >/dev/null || sudo apt-get install -y gdisk >/dev/null 2>&1 || true

# 1. QEMU: the twin's own build (11.1.2 with its sound patch), carried with its
#    libraries in qemu.tgz, so nothing has to be installed system-wide.
if ! [ -x "$HERE/qemu/bin/qemu-system-aarch64" ]; then
  echo "== unpacking QEMU"
  tar xzf "$HERE/qemu.tgz" -C "$HERE"
fi
"$HERE/qemu/bin/qemu-system-aarch64" --version | head -1

# 2. the card: Knulli's image in an 8 GB file (Knulli grows into the rest at its first start)
mkdir -p "$HERE/run"
if ! [ -f "$HERE/run/sd.img" ]; then
  echo "== making run/sd.img (8 GB) from $IMG"
  cp "$IMG" "$HERE/run/sd.img"
  truncate -s 8G "$HERE/run/sd.img"
  command -v sgdisk >/dev/null && sgdisk -e "$HERE/run/sd.img" >/dev/null 2>&1 || true   # (moves the backup partition table to the new end)
fi
cp -f "$HERE/initrd.gz" "$HERE/run/initrd.gz"
echo "== ready. Start it with:  sh $HERE/start.sh &"
echo "   First start takes a minute or two (Knulli sets its card up). Then put Zappo X on it:  sh $HERE/zx-install.sh"
