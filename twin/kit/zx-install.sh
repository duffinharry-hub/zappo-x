#!/bin/sh
# Puts Zappo X onto the running twin - the same package the PC kit writes to a
# card (zappo-x-knulli.tar: program, start loop, fonts, themes, lock screens,
# install.sh) - and starts it. Run again to update; settings are carried over.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
T="sh $HERE/twinctl"
echo "== copying the package in"
$T put "$HERE/zappo-x-knulli.tar" /tmp/zappo-x-knulli.tar >/dev/null
echo "== installing"
$T sh '/userdata/system/services/zappox stop 2>/dev/null; killall zappo-x.sh zappo-x 2>/dev/null; sleep 1
rm -rf /tmp/zx && mkdir -p /tmp/zx && tar -xf /tmp/zappo-x-knulli.tar -C /tmp/zx
cd /tmp/zx && sh ./install.sh && cp uninstall.sh /userdata/system/zappo-x/ && chmod +x /userdata/system/zappo-x/uninstall.sh
# Knulli'"'"'s own menu lets go of the screen (on a card it happens at boot, before the menu)
if pidof emulationstation >/dev/null; then
  emulationstation-standalone --stop-rebooting 2>/dev/null; killall emulationstation 2>/dev/null
  n=0; while pidof emulationstation >/dev/null && [ $n -lt 50 ]; do sleep 0.2; n=$((n + 1)); done
fi
/userdata/system/services/zappox start; sleep 4; /userdata/system/services/zappox status' 180
echo "Zappo X is on the twin and starts at every boot. Screen: sh $HERE/twinctl shot out.png"
