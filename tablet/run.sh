#!/data/data/com.termux/files/usr/bin/sh
# Starts Zappo X (if it isn't running already) and opens it. Once Zappo X is
# installed as an app (Chrome: Install app), its home-screen icon does the
# same without Termux; this is for the first time, or after it was stopped.
cd "$(dirname "$0")/.." || exit 1
sh tablet/server.sh start || exit 1
# a moment for it to be ready
i=0
while [ $i -lt 20 ]; do
	if command -v curl >/dev/null; then curl -s -o /dev/null http://127.0.0.1:8640/manifest.webmanifest && break; else sleep 2; break; fi
	sleep 0.5
	i=$((i + 1))
done
termux-open-url http://127.0.0.1:8640/ 2>/dev/null || echo "Open http://127.0.0.1:8640 in Chrome"
