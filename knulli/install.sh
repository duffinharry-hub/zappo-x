#!/bin/sh
# Installs Zappo X on a Knulli handheld from the folder this script is in.
# Nothing of Knulli's own is changed: Zappo X lives in /userdata/system/zappo-x,
# starts through a Knulli user service, and Knulli's menu is only told not to
# start at boot (system.es.atstartup=0). uninstall.sh puts that back.
# An earlier Zappo X is moved aside (zappo-x.old-<date>), never deleted;
# settings, favourites and recents are carried over.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
D=/userdata/system/zappo-x
if [ -d "$D" ]; then
	OLD=$D.old-$(date +%Y%m%d-%H%M%S)
	mv "$D" "$OLD"
	echo "previous Zappo X moved to $OLD"
fi
mkdir -p "$D" /userdata/system/services /userdata/music /userdata/import /userdata/videos /userdata/roms/ports/images
cp -a "$HERE/zappo-x/." "$D/"
if [ -n "$OLD" ]; then
	for f in settings.json recents.json favorites.lpl input.json systems.json play-counts.tsv music-state.tsv music-cache.tsv music-list.tsv battery-log.tsv runs.log wifi.log watch-later themes; do
		if [ -d "$OLD/$f" ]; then mkdir -p "$D/$f" && cp -a "$OLD/$f/." "$D/$f/" 2>/dev/null || true
		elif [ -e "$OLD/$f" ]; then cp -a "$OLD/$f" "$D/" 2>/dev/null || true; fi
	done
fi
cp "$HERE/zappox" /userdata/system/services/zappox
# the way back from Knulli's menu: Ports > Zappo X
cp "$HERE/Zappo X.sh" "/userdata/roms/ports/Zappo X.sh"
chmod +x "/userdata/roms/ports/Zappo X.sh"
[ -f "$D/zappos/electric-m.png" ] && cp "$D/zappos/electric-m.png" "/userdata/roms/ports/images/Zappo X-image.png"
chmod +x "$D/zappo-x" "$D/zappo-x.sh" /userdata/system/services/zappox
batocera-settings-set system.es.atstartup 0
batocera-services enable zappox >/dev/null
sync
echo "Zappo X installed; it starts at the next boot (or now: /userdata/system/services/zappox start)"
