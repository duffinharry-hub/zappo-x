#!/data/data/com.termux/files/usr/bin/sh
# Keeps Zappo X running in the background, so the Zappo X app (its icon on
# the home screen) opens straight into it.
#   server.sh start     starts it if it isn't running (Termux does this each
#                       time it opens, and at switch-on with Termux:Boot)
#   server.sh stop
#   server.sh restart   (after an update: the new version takes over)
#   server.sh status
#   server.sh           runs it here, starting it again whenever it stops
cd "$(dirname "$0")/.." || exit 1
DIR="$(pwd)"
RUN="${TMPDIR:-/data/data/com.termux/files/usr/tmp}"
mkdir -p "$RUN"
LOOP="$RUN/zappo-x-server.pid"   # this script's loop
APP="$RUN/zappo-x.pid"           # Zappo X itself
LOG="$RUN/zappo-x.log"
running() { [ -f "$LOOP" ] && kill -0 "$(cat "$LOOP")" 2>/dev/null; }

case "$1" in
start)
	running && exit 0
	[ -x build/zappo-tablet ] || { echo "Zappo X isn't built yet: sh ~/zappo-x/tablet/install.sh"; exit 1; }
	nohup sh "$DIR/tablet/server.sh" >/dev/null 2>&1 &
	exit 0 ;;
stop)
	running && kill "$(cat "$LOOP")" 2>/dev/null
	[ -f "$APP" ] && kill "$(cat "$APP")" 2>/dev/null
	rm -f "$LOOP" "$APP"
	exit 0 ;;
restart)
	if running; then [ -f "$APP" ] && kill "$(cat "$APP")" 2>/dev/null; else sh "$DIR/tablet/server.sh" start; fi
	exit 0 ;;
status)
	if running; then echo "Zappo X is running: http://127.0.0.1:8640"; else echo "Zappo X isn't running"; fi
	exit 0 ;;
esac

# the loop
running && { echo "Zappo X is already running"; exit 0; }
echo $$ > "$LOOP"
child=""
trap '[ -n "$child" ] && kill $child 2>/dev/null; rm -f "$LOOP" "$APP"; exit 0' TERM INT HUP
[ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1000000 ] && mv -f "$LOG" "$LOG.old"
while :; do
	ZAPPO_WEB=tablet/web ./build/zappo-tablet >> "$LOG" 2>&1 &
	child=$!
	echo "$child" > "$APP"
	wait "$child"
	child=""
	sleep 1
done
