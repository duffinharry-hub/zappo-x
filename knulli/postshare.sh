#!/bin/bash
# Zappo X kit: Knulli runs this from its boot partition at every start-up, as
# soon as the data partition (/userdata) is ready - before its own menu would
# start. It only does something when the PC kit has left work for it in
# /boot/zappo-x-kit:
#   install/    Zappo X to install (or update): its install.sh is run
#   test-pack/  songs and films to copy to the card (only ones not already there)
#   remove      take Zappo X off: Knulli's own menu at the next start
# What it did goes in /boot/zappo-x-kit/log.txt (readable on the PC).
# A postshare.sh the card had before is kept as postshare.before-zappo-x.sh and
# still runs, after this. Nothing is deleted: done work is moved aside.
B=/boot
K=$B/zappo-x-kit
before() { [ -f $B/postshare.before-zappo-x.sh ] && bash $B/postshare.before-zappo-x.sh "$@"; }

if [ "$1" != start ] || [ ! -d $K ]; then before "$@"; exit 0; fi
work=
[ -d $K/install ] || [ -d $K/test-pack ] || [ -f $K/remove ] || [ -f $K/survey-tf2.sh ] && work=1
[ -z "$work" ] && { before "$@"; exit 0; }

mount -o remount,rw $B 2>/dev/null
LOG=$K/log.txt
say() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> $LOG; }
stamp=$(date +%Y%m%d-%H%M%S)
mkdir -p $K/done

if [ -f $K/remove ]; then
	say "taking Zappo X off"
	if [ -x /userdata/system/zappo-x/uninstall.sh ]; then sh /userdata/system/zappo-x/uninstall.sh >> $LOG 2>&1
	elif [ -f $K/installed/uninstall.sh ]; then sh $K/installed/uninstall.sh >> $LOG 2>&1
	else
		batocera-services disable zappox >/dev/null 2>&1
		batocera-settings-set system.es.atstartup 1
	fi
	mv $K/remove $K/done/remove-$stamp
	# this hook's work is over: the card's own one (if any) goes back in its place
	mv $B/postshare.sh $K/done/postshare.zappo-x.sh
	[ -f $B/postshare.before-zappo-x.sh ] && mv $B/postshare.before-zappo-x.sh $B/postshare.sh
	say "done: Knulli's own menu starts (Zappo X's folder, with your settings, stays in /userdata/system/zappo-x)"
	sync
	mount -o remount,ro $B 2>/dev/null
	[ -f $B/postshare.sh ] && bash $B/postshare.sh "$@"
	exit 0
fi

if [ -d $K/install ]; then
	say "installing Zappo X ($(cat $K/install/zappo-x/VERSION 2>/dev/null || echo '?'))"
	if (cd $K/install && sh ./install.sh) >> $LOG 2>&1; then
		cp $K/install/uninstall.sh /userdata/system/zappo-x/uninstall.sh 2>/dev/null
		chmod +x /userdata/system/zappo-x/uninstall.sh 2>/dev/null
		[ -d $K/installed ] && rm -rf $K/installed	# (the kit's own copy from last time)
		mv $K/install $K/installed
		say "installed: Zappo X starts in a moment"
	else
		mv $K/install $K/done/install-failed-$stamp
		say "the install did not work (above); Knulli's menu is unchanged"
	fi
fi

if [ -d $K/test-pack ]; then
	say "copying the test pack"
	for kind in music videos; do
		[ -d $K/test-pack/$kind ] || continue
		dst="/userdata/$kind/Zappo Test Pack"
		mkdir -p "$dst"
		for f in $K/test-pack/$kind/*; do
			[ -e "$f" ] || continue
			n=$(basename "$f")
			if [ -e "$dst/$n" ]; then say "  $kind/$n is already there"; else cp -a "$f" "$dst/" && say "  $kind/$n"; fi
		done
	done
	mv $K/test-pack $K/done/test-pack-$stamp
fi

if [ -f $K/survey-tf2.sh ]; then
	say "looking at the second card (TF2), read-only"
	sh $K/survey-tf2.sh > $K/tf2-survey.txt 2>&1
	mv $K/survey-tf2.sh $K/done/survey-tf2-$stamp.sh
	say "done: zappo-x-kit/tf2-survey.txt (nothing on TF2 was changed)"
fi

sync
mount -o remount,ro $B 2>/dev/null
before "$@"
exit 0
