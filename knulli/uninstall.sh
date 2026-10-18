#!/bin/sh
# Gives the handheld back to Knulli's own menu. Zappo X's folder stays
# (/userdata/system/zappo-x, with your settings); delete it yourself if wanted.
/userdata/system/services/zappox stop 2>/dev/null
batocera-services disable zappox >/dev/null 2>&1
batocera-settings-set system.es.atstartup 1
mkdir -p /userdata/system/zappo-x/removed-from-ports
mv "/userdata/roms/ports/Zappo X.sh" /userdata/system/zappo-x/removed-from-ports/ 2>/dev/null
sync
echo "Knulli's menu starts at the next boot (or now: /etc/init.d/S31emulationstation start)"
