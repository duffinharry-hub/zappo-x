#!/data/data/com.termux/files/usr/bin/sh
# Zappo X on an Android tablet or phone, at full speed (Termux).
# Builds Zappo for this device, sets up its test card in ~/zappo-card, and
# adds Python for Send to Zappo and Get box art.
# Run once after downloading, and again after each update.
set -e
cd "$(dirname "$0")/.."
command -v clang >/dev/null || pkg install -y clang make
# Send to Zappo and Get box art are small Python programs; Pillow makes the
# box art pictures smaller (they still work without it, just bigger)
command -v python3 >/dev/null || pkg install -y python
python3 -c 'import PIL' 2>/dev/null || pkg install -y python-pillow || echo "(no Pillow: box art is saved at full size)"
make tablet CC_TABLET=clang
CARD="${ZAPPO_CARD:-$HOME/zappo-card}"
./build/zappo-tablet --unpack tablet/card "$CARD"
mkdir -p "$CARD/mmc/CFW/clove"
cp knulli/send-to-zappo.py knulli/get-box-art.py "$CARD/mmc/CFW/clove/"
echo
echo "Done. Start Zappo X with:  sh ~/zappo-x/tablet/run.sh"
