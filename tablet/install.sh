#!/data/data/com.termux/files/usr/bin/sh
# Zappo X on an Android tablet or phone, at full speed (Termux).
# Builds Zappo for this device and sets up its test card in ~/zappo-card.
# Run once after downloading, and again after each update.
set -e
cd "$(dirname "$0")/.."
command -v clang >/dev/null || pkg install -y clang make
make tablet CC_TABLET=clang
./build/zappo-tablet --unpack tablet/card "$HOME/zappo-card"
echo
echo "Done. Start Zappo X with:  ~/zappo-x/tablet/run.sh"
