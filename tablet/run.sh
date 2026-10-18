#!/data/data/com.termux/files/usr/bin/sh
# Starts Zappo X and opens it in the browser. Leave Termux running in the
# background; close Zappo with Ctrl+C in Termux (or the Volume Down + C keys).
cd "$(dirname "$0")/.."
# keeps Zappo (and Send to Zappo, Get box art) going while Chrome is in front
termux-wake-lock 2>/dev/null
trap 'termux-wake-unlock 2>/dev/null' EXIT
ZAPPO_WEB=tablet/web ./build/zappo-tablet &
sleep 1
termux-open-url http://127.0.0.1:8640/ 2>/dev/null || echo "Open http://127.0.0.1:8640 in Chrome"
wait
