#!/bin/sh
# Back to Zappo X, from Knulli's menu (installed in roms/ports by Zappo X's
# install.sh). If Knulli's menu was opened from Zappo X, closing it is enough;
# if Knulli's menu was running on its own (Zappo X switched off in Settings),
# Zappo X is switched back on and started.
D=/userdata/system/zappo-x
rm -f $D/disabled
batocera-settings-set system.es.atstartup 0
batocera-services enable zappox >/dev/null 2>&1
if [ -f /var/run/zappo-x.pid ] && kill -0 "$(cat /var/run/zappo-x.pid)" 2>/dev/null; then
	killall emulationstation
else
	emulationstation-standalone --stop-rebooting
	killall emulationstation
	n=0   # until Knulli's menu has let go of the screen
	while pidof emulationstation >/dev/null && [ $n -lt 50 ]; do sleep 0.2; n=$((n + 1)); done
	/userdata/system/services/zappox start
fi
