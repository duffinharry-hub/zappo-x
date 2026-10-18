#!/bin/sh
# CLOVE_WRAPPER
# GarlicOS's start script runs CFW/main in a loop. Clove's installer moved the
# original GarlicOS launcher to CFW/main.garlic and put this script in its place.
cd /mnt/mmc/CFW

# GarlicOS calls "main restorecpu" once at boot: let the original handle it.
if [ "$1" = "restorecpu" ]; then
	[ -f ./main.garlic ] && exec ./main.garlic restorecpu
	exit 0
fi

# Clove switched off (Settings > About > Switch to the GarlicOS menu) or removed.
if [ -f ./clove/disabled ] || [ ! -f ./clove/clove ]; then
	[ -f ./main.garlic ] && exec ./main.garlic "$@"
	sleep 2
	exit 0
fi

# A command.sh still here now is left over from a game or video that already
# ran (or from a reset while it was playing): without this it would start
# again by itself.
rm -f ./retroarch/command.sh 2>/dev/null

# a short diary of starts and stops, for finding out what happened after a crash
RUNS=./clove/runs.log
UP=$(cut -d' ' -f1 /proc/uptime 2>/dev/null | cut -d. -f1)
# A fresh boot right after something was started from Zappo means the handheld
# restarted while it ran: leave a quiet note for Zappo to show.
LASTLINE=$(busybox tail -n 1 $RUNS 2>/dev/null)
case "$LASTLINE" in
	*"clove exit"*"next: "*)
		if [ "${UP:-999}" -lt 240 ]; then
			WHAT=$(echo "$LASTLINE" | busybox sed -n "s/.*retroarch -L \"[^\"]*\" '\(.*\)'.*/\1/p" | busybox sed 's#.*/##; s/\.[^.]*$//')
			echo "The handheld restarted while running ${WHAT:-the last game or video}. Its logs are kept in CFW/clove." > ./clove/trouble.txt
		fi ;;
esac
echo "$(date) start (uptime ${UP}s)" >> $RUNS 2>/dev/null

# a copy of the saves once a week (Backups/saves-<date> at the top of the card, newest three kept)
if [ -d ./retroarch/.retroarch -o -d ../Saves ]; then
	B=../Backups
	mkdir -p $B
	if [ -z "$(busybox find $B -maxdepth 1 -name 'saves-*' -mtime -7 2>/dev/null)" ]; then
		D=$B/saves-$(date +%Y%m%d)
		mkdir -p $D
		[ -d ../Saves/CurrentProfile/saves ] && cp -r ../Saves/CurrentProfile/saves $D/ 2>/dev/null
		[ -d ../Saves/CurrentProfile/states ] && cp -r ../Saves/CurrentProfile/states $D/ 2>/dev/null
		[ -d ./retroarch/.retroarch/saves ] && cp -r ./retroarch/.retroarch/saves $D/retroarch-saves 2>/dev/null
		[ -d ./retroarch/.retroarch/states ] && cp -r ./retroarch/.retroarch/states $D/retroarch-states 2>/dev/null
		cp ./clove/settings.json ./clove/recents.json ./clove/play-counts.tsv $D/ 2>/dev/null
		ls -d $B/saves-* 2>/dev/null | sort | busybox head -n -3 | while read -r old; do rm -rf "$old"; done
		sync
	fi
fi
if [ "$(busybox wc -l < $RUNS 2>/dev/null || echo 0)" -gt 200 ]; then
	busybox tail -n 100 $RUNS > $RUNS.tmp 2>/dev/null && mv $RUNS.tmp $RUNS
fi

# after a sudden restart the kernel keeps its last messages: save them (they say why)
if [ -r /proc/last_kmsg ]; then
	{ echo "$(date) previous kernel log:"; busybox tail -n 300 /proc/last_kmsg; } > ./clove/last-kmsg.log 2>/dev/null
fi
for f in /sys/fs/pstore/*; do [ -r "$f" ] && cat "$f" >> ./clove/last-kmsg.log 2>/dev/null; done
# and the player's own log from the previous run, before RetroArch overwrites it
[ -f ./retroarch/.retroarch/logs/retroarch.log ] && cp ./retroarch/.retroarch/logs/retroarch.log ./clove/retroarch-last.log 2>/dev/null

# (FAT cards ignore this; it matters if the files were copied without execute permission)
[ -x ./clove/clove ] || chmod 755 ./clove/clove 2>/dev/null
./clove/clove "$@" 2>./clove/stderr.log
RC=$?
{
	echo "$(date) clove exit $RC; next: $(busybox tail -n 1 ./retroarch/command.sh 2>/dev/null | busybox cut -c1-160)"
	mount 2>/dev/null | grep -q " /mnt/mmc .*[(,]ro[,)]" && echo "$(date) WARNING: the card is mounted read-only (file system error)"
} >> $RUNS 2>/dev/null
if [ $RC -ne 0 ] && [ ! -f ./retroarch/command.sh ]; then
	# Clove couldn't start. Note why (for fixing it), then try once more from a
	# copy in memory, and if that fails too use the GarlicOS menu.
	{
		echo "$(date) clove exited with code $RC"
		echo "-- clove's own messages:"; cat ./clove/stderr.log
		echo "-- the program file as this handheld sees it:"
		ls -l ./clove/clove; busybox md5sum ./clove/clove; busybox head -c 16 ./clove/clove | busybox od -An -tx1
		echo "-- screen:"; ls -l /dev/fb0; cat /proc/fb
		echo "-- memory:"; busybox free
		echo "-- cards:"; mount | grep -E "mmc|SDCARD"
		echo "-- kernel messages:"; dmesg | busybox tail -n 30
	} >> ./clove/crash.log 2>&1
	if cp ./clove/clove /tmp/clove-run 2>/dev/null && chmod 755 /tmp/clove-run; then
		/tmp/clove-run "$@" 2>>./clove/stderr.log
		RC2=$?
		rm -f /tmp/clove-run
		echo "$(date) second try (from memory) exited with code $RC2" >> ./clove/crash.log
		if [ $RC2 -eq 0 ] || [ -f ./retroarch/command.sh ]; then exit 0; fi
		echo "-- clove's messages, second try:" >> ./clove/crash.log; cat ./clove/stderr.log >> ./clove/crash.log
	fi
	echo "$(date) used the GarlicOS menu instead" >> ./clove/crash.log
	[ -f ./main.garlic ] && exec ./main.garlic "$@"
	sleep 2
fi
exit 0
