#!/data/data/com.termux/files/usr/bin/sh
# Zappo X on an Android tablet or phone, at full speed (Termux).
# Builds Zappo for this device, sets up its card ("Zappo X" in the tablet's
# own storage, where RetroArch can play the games and a PC sees it over USB),
# and adds Python for Send to Zappo and Get box art.
# Run once after downloading, and again after each update.
set -e
cd "$(dirname "$0")/.."
command -v clang >/dev/null || pkg install -y clang make
# Send to Zappo and Get box art are small Python programs; Pillow makes the
# box art pictures smaller (they still work without it, just bigger)
command -v python3 >/dev/null || pkg install -y python
python3 -c 'import PIL' 2>/dev/null || pkg install -y python-pillow || echo "(no Pillow: box art is saved at full size)"

# the tablet's own storage: Android asks once
if [ ! -d "$HOME/storage/shared" ] && command -v termux-setup-storage >/dev/null; then
	echo
	echo "Android will ask whether Termux may see your files: tap Allow."
	termux-setup-storage || true
	i=0
	while [ ! -d "$HOME/storage/shared" ] && [ $i -lt 60 ]; do sleep 1; i=$((i + 1)); done
	[ -d "$HOME/storage/shared" ] || echo "(not allowed: the card stays in Termux's own folder, and games can't start in RetroArch)"
fi

make tablet CC_TABLET=clang
CARD="$(./build/zappo-tablet --card-path)"
mkdir -p "$CARD"
if [ "$CARD" != "$HOME/zappo-card" ] && [ -d "$HOME/zappo-card/mmc" ]; then OLD="$HOME/zappo-card"; else OLD=""; fi
case "$CARD" in
"$HOME/zappo-card") ./build/zappo-tablet --unpack tablet/card "$CARD" ;;
*) ./build/zappo-tablet --unpack tablet/card "$CARD" --no-stand-ins ;;	# (real games: no pretend ones)
esac
python3 tablet/setup-card.py "$CARD" "$OLD"
mkdir -p "$CARD/mmc/CFW/clove"
cp knulli/send-to-zappo.py knulli/get-box-art.py "$CARD/mmc/CFW/clove/"
echo
echo "Done. Zappo X's card: $CARD"
echo "Games go in its mmc/Roms/<console> folders (GBA, SFC, MD, PS ...), or send them with Send to Zappo."
echo "Start Zappo X with:  sh ~/zappo-x/tablet/run.sh"
