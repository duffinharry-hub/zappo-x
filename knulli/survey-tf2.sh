#!/bin/sh
# Zappo X kit: a READ-ONLY look at the second card (TF2), so the PC kit can see
# how its games are laid out. Nothing on TF2 is ever written: partitions that
# aren't mounted yet are mounted read-only here and unmounted again; ones Knulli
# already mounted are only read. Run once by postshare.sh; output in tf2-survey.txt.
echo "== $(date '+%Y-%m-%d %H:%M:%S') TF2 survey (read-only)"
ud=$(awk '$2=="/userdata"{print $1}' /proc/mounts | head -n1)
boot=$(echo "$ud" | sed -E 's#^/dev/##; s#p?[0-9]+$##')
echo "Knulli card: $boot (userdata on $ud)"
echo "-- block devices"
cat /proc/partitions
command -v blkid >/dev/null && blkid 2>/dev/null
echo "-- mounted now"
grep -E '^/dev/(mmcblk|sd)' /proc/mounts
for p in /dev/mmcblk[0-9]p[0-9]* /dev/sd[a-z][0-9]*; do
	[ -b "$p" ] || continue
	case "$p" in /dev/${boot}p*|/dev/${boot}[0-9]*) continue ;; esac
	echo
	echo "== partition $p  $(blkid "$p" 2>/dev/null | cut -d: -f2-)"
	mp=$(awk -v d="$p" '$1==d{print $2; exit}' /proc/mounts)
	mine=
	if [ -z "$mp" ]; then
		mp=/tmp/zx-tf2-survey/$(basename "$p"); mkdir -p "$mp"
		if mount -o ro "$p" "$mp" 2>/dev/null; then mine=1; else echo "   (can't be read: no file system Linux knows, or a raw partition)"; rmdir "$mp" 2>/dev/null; continue; fi
	fi
	echo "   read at $mp$([ -n "$mine" ] && echo ' (mounted read-only for this look)')"
	df -h "$mp" | tail -n1
	echo "   top level:"; ls -la "$mp" 2>/dev/null | head -n 60
	find "$mp" -maxdepth 3 -type d \( -iname roms -o -iname bios -o -iname imgs -o -iname saves -o -iname 'save*' -o -iname apps -o -iname ports \) 2>/dev/null | while read -r d; do
		echo "   -- $d"
		for s in "$d"/*/; do
			[ -d "$s" ] || continue
			n=$(find "$s" -type f 2>/dev/null | wc -l); sz=$(du -sh "$s" 2>/dev/null | cut -f1)
			ex=$(find "$s" -maxdepth 1 -type f 2>/dev/null | head -n 3 | sed 's#.*/##' | tr '\n' '|')
			echo "      $(basename "$s"): $n files, $sz   e.g. $ex"
		done
		find "$d" -maxdepth 1 -type f 2>/dev/null | head -n 20 | sed 's#^#      file: #'
	done
	[ -n "$mine" ] && { umount "$mp" 2>/dev/null; rmdir "$mp" 2>/dev/null; }
done
echo "== survey done"
