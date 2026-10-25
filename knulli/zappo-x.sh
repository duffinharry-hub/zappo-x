#!/bin/sh
# Zappo X on Knulli: runs Zappo X in place of EmulationStation (started at
# boot by the "zappox" service). When a game is chosen, Zappo X writes
# command.sh and quits; this runs it (Knulli's emulatorlauncher, like its own
# menu does) and brings Zappo X back when the game ends.
#
# Knulli's menu instead: Settings > About > Switch to Knulli's menu (leaves a
# file "disabled" here), or if Zappo X can't start (5 quick failures in a row).
D=/userdata/system/zappo-x
cd "$D" || exit 1
[ -f /etc/profile.d/xdg.sh ] && . /etc/profile.d/xdg.sh    # sound (PipeWire) for the music player
[ -f /etc/profile.d/dbus.sh ] && . /etc/profile.d/dbus.sh
export HOME=/userdata/system
RUNS=$D/runs.log
CMD=/tmp/zappo-x-command.sh

knulli_menu() {
	echo "$(date) Knulli's menu ($1)" >> $RUNS
	rm -f /var/run/zappo-x.pid   # (Zappo X isn't running any more: see "Zappo X.sh" in Ports)
	knulli-rgb-led-daemon import >/dev/null 2>&1   # the stick lights: Knulli's own colours again
	exec emulationstation-standalone
}

# The TV (HDMI) while one is plugged in, else the handheld's screen - as
# Knulli's own start does. Only when that changed: switching makes the screen
# blink. (Zappo X exits with 5 when the cable goes in or out.)
OUTPUT=
apply_output() {
	s=$(cat /sys/devices/platform/soc/6000000.hdmi/extcon/hdmi/state 2>/dev/null)
	[ "$s" = "$OUTPUT" ] && return
	OUTPUT=$s
	batocera-resolution setOutput >/dev/null 2>&1
	echo "$(date) screen: ${s:-the handheld's}" >> $RUNS
}

echo $$ > /var/run/zappo-x.pid
trap 'rm -f /var/run/zappo-x.pid; exit 0' TERM INT
# Wait (up to 5 s) until nothing else has the screen: a program that has
# just ended can still be letting go of it.
screen_free() {
	n=0
	while [ $n -lt 25 ]; do
		# (one ls for every open file: a readlink each took seconds)
		ls -l /proc/[0-9]*/fd 2>/dev/null | grep -q ' -> /dev/dri/card' || return 0
		sleep 0.2; n=$((n + 1))
	done
}

# An update Zappo X said yes to (Settings > About): the package waiting in
# /userdata/system/zappo-x-update is installed by its own install.sh (the
# old Zappo X is moved aside, settings carried over), then this loop starts
# again from the new folder. The package is kept under installed/.
U=/userdata/system/zappo-x-update
install_update() {
	rm -f "$D/update-now"
	T=$U/zappo-x-knulli.tar
	[ -f "$T" ] || { echo "$(date) update: nothing in $U" >> $RUNS; return; }
	echo "$(date) update: installing $(cat $T.version 2>/dev/null)" >> $RUNS
	rm -rf /tmp/zappo-x-update && mkdir -p /tmp/zappo-x-update
	if tar -C /tmp/zappo-x-update -xf "$T" && sh /tmp/zappo-x-update/install.sh >> $RUNS 2>&1; then
		mkdir -p $U/installed
		mv -f "$T" "$U/installed/zappo-x-knulli-$(date +%Y%m%d-%H%M%S).tar"
		mv -f "$T.version" "$U/installed/" 2>/dev/null
		echo "$(date) update: done, starting the new Zappo X" >> $RUNS
		sync
		exec "$D/zappo-x.sh"
	fi
	echo "$(date) update: install.sh failed; the package is still there" >> $RUNS
}

# something left from before a restart would start by itself: not wanted
rm -f "$D/command.sh" "$D/update-now"
echo "$(date) start" >> $RUNS
fails=0
while :; do
	[ -f "$D/disabled" ] && knulli_menu "switched in Settings"
	screen_free
	apply_output
	t0=$(cut -d. -f1 /proc/uptime)
	"$D/zappo-x"
	rc=$?
	t1=$(cut -d. -f1 /proc/uptime)
	if [ -f "$D/command.sh" ]; then
		mv -f "$D/command.sh" $CMD
		echo "$(date) run: $(grep -v '^#' $CMD | tail -n 1 | cut -c1-200)" >> $RUNS
		sync
		sh $CMD
		echo "$(date) back (exit $?)" >> $RUNS
		fails=0
		continue
	fi
	echo "$(date) Zappo X exit $rc after $((t1 - t0)) s" >> $RUNS
	[ -f "$D/update-now" ] && install_update
	[ $rc = 4 ] && knulli_menu "buttons not recognised"
	if [ $rc = 5 ]; then OUTPUT=; continue; fi   # the screen changed (TV in or out, its picture size)
	if [ $rc != 0 ] && [ $((t1 - t0)) -lt 10 ]; then
		fails=$((fails + 1))
		[ $fails -ge 5 ] && knulli_menu "Zappo X failed to start 5 times"
		sleep 1
	else
		fails=0
	fi
	# keep the diary short
	if [ "$(wc -l < $RUNS)" -gt 400 ]; then tail -n 200 $RUNS > $RUNS.tmp && mv $RUNS.tmp $RUNS; fi
done
