#!/bin/sh
# Zappo X kit for Linux and macOS: the same as "Zappo X.bat".
#   ./zappo-x-kit.sh install [CARD]     install / update Zappo X on the card
#   ./zappo-x-kit.sh testpack [CARD]    add the test pack (songs and films)
#   ./zappo-x-kit.sh check [CARD]       read it back, show what the handheld did
#   ./zappo-x-kit.sh remove [CARD]      take Zappo X off (Knulli's menu at the next start)
# CARD: where the Knulli card's BATOCERA partition is mounted (found by itself
# under /media, /run/media and /Volumes when there is just one).
set -e
KIT=$(cd "$(dirname "$0")" && pwd)
cmd=${1:-}
card=${2:-}
say() { echo "  $*"; mkdir -p "$KIT/logs"; echo "$*" >> "$KIT/logs/zappo-x-kit-log.txt"; }
is_knulli() { [ -f "$1/boot/batocera" ] && [ -f "$1/boot/batocera.board" ]; }
if [ -z "$card" ]; then
	n=0
	for d in /media/*/* /media/* /run/media/*/* /Volumes/*; do
		[ -d "$d" ] && is_knulli "$d" && { card=$d; n=$((n + 1)); }
	done
	[ $n -eq 0 ] && { echo "No Knulli card found: mount its BATOCERA partition, or give its path." >&2; exit 1; }
	[ $n -gt 1 ] && { echo "More than one Knulli card is mounted: give the path of the one to use." >&2; exit 1; }
fi
is_knulli "$card" || { echo "$card is not a Knulli card's BATOCERA partition (no boot/batocera there)" >&2; exit 1; }
K="$card/zappo-x-kit"
MARK='Zappo X kit: Knulli runs this'
md5() { if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi; }
copy_tree() {	# copy_tree FROM TO: each file checked after copying
	(cd "$1" && find . -type f) | while IFS= read -r f; do
		src="$1/$f"; dst="$2/$f"
		[ -f "$dst" ] && [ "$(md5 "$dst")" = "$(md5 "$src")" ] && continue
		mkdir -p "$(dirname "$dst")"
		cp "$src" "$dst.part"
		[ "$(md5 "$dst.part")" = "$(md5 "$src")" ] || { rm -f "$dst.part"; echo "could not copy $f" >&2; exit 1; }
		mv "$dst.part" "$dst"
	done
}
hook() {
	if [ -f "$card/postshare.sh" ] && ! grep -q "$MARK" "$card/postshare.sh"; then
		[ -f "$card/postshare.before-zappo-x.sh" ] && { echo "the card has its own postshare.sh and a postshare.before-zappo-x.sh: sort those out first" >&2; exit 1; }
		mkdir -p "$KIT/backups/card" && cp "$card/postshare.sh" "$KIT/backups/card/postshare.sh"
		mv "$card/postshare.sh" "$card/postshare.before-zappo-x.sh"
		say "The card's own postshare.sh is kept as postshare.before-zappo-x.sh (it still runs)."
	fi
	cp "$KIT/card/postshare.sh" "$card/postshare.sh"
}
stamp=$(date +%Y%m%d-%H%M%S)
case "$cmd" in
install|testpack)
	hook
	mkdir -p "$K/done"
	[ -f "$K/remove" ] && { mv "$K/remove" "$K/done/remove-cancelled-$stamp"; say 'A waiting "take Zappo X off" is cancelled.'; }
	if [ "$cmd" = install ]; then copy_tree "$KIT/card/zappo-x-kit/install" "$K/install"; say "Zappo $(head -n1 "$KIT/card/zappo-x-kit/install/zappo-x/VERSION") is on the card."; fi
	if [ "$cmd" = testpack ]; then copy_tree "$KIT/test-pack" "$K/test-pack"; say "The test pack is on the card (it goes into Music and Videos at the next start)."; fi
	sync
	say "DONE. Unmount the card, put it in the handheld and switch on (a new Knulli card sets itself up first)."
	;;
remove)
	hook
	mkdir -p "$K/done"
	[ -d "$K/install" ] && mv "$K/install" "$K/done/install-not-used-$stamp"
	echo "asked for $(date '+%Y-%m-%d %H:%M')" > "$K/remove"
	sync
	say "DONE. At the next start Zappo X steps aside and Knulli's own menu starts (your settings stay)."
	;;
check)
	say "Knulli card at $card (board: $(cat "$card/boot/batocera.board" 2>/dev/null))"
	if grep -qs "$MARK" "$card/postshare.sh"; then say "Start-up hook: present"; else say "No Zappo X start-up hook on this card"; fi
	for w in install installed test-pack; do [ -d "$K/$w" ] && say "zappo-x-kit/$w: $(find "$K/$w" -type f | wc -l | tr -d ' ') files"; done
	[ -f "$K/remove" ] && say "Taking Zappo X off is waiting for the next start."
	[ -f "$K/log.txt" ] && { say "What the handheld did:"; tail -n 25 "$K/log.txt" | sed 's/^/    /'; }
	;;
*) sed -n '2,9p' "$0"; exit 1 ;;
esac
