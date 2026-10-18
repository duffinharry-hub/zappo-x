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
	exec emulationstation-standalone
}

echo $$ > /var/run/zappo-x.pid
trap 'rm -f /var/run/zappo-x.pid; exit 0' TERM INT
# something left from before a restart would start by itself: not wanted
rm -f "$D/command.sh"
echo "$(date) start" >> $RUNS
fails=0
while :; do
	[ -f "$D/disabled" ] && knulli_menu "switched in Settings"
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
	[ $rc = 4 ] && knulli_menu "buttons not recognised"
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
