#!/data/data/com.termux/files/usr/bin/sh
# Gets the latest Zappo X and rebuilds it.
cd "$(dirname "$0")/.." && git pull --ff-only && sh tablet/install.sh
