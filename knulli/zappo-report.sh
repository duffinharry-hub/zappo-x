#!/bin/sh
# A report about this handheld for working out what went wrong: Zappo X's
# diary, its Wi-Fi log, Knulli's settings (passwords hidden), the network,
# the controller. Nothing private goes in. Prints it, or writes it to $1.
D=/userdata/system/zappo-x
out() {
	echo "Zappo X report, $(date)"
	echo "version: $(cat $D/VERSION 2>/dev/null || echo '?')"
	echo "knulli: $(cat /usr/share/batocera/batocera.version 2>/dev/null | head -n 1)  board: $(cat /boot/boot/batocera.board 2>/dev/null)"
	echo "uptime: $(cut -d' ' -f1 /proc/uptime) s   model: $(tr -d '\0' < /proc/device-tree/model 2>/dev/null)"
	echo
	echo "== settings (batocera.conf, without the comment lines; keys hidden)"
	grep -v '^#' /userdata/system/batocera.conf 2>/dev/null | grep -v '^[[:space:]]*$' | sed 's/\(\.key=\).*/\1<hidden>/; s/\(password=\).*/\1<hidden>/; s/\(\.token=\).*/\1<hidden>/'
	echo
	echo "== network"
	ip -o link 2>/dev/null; ip -4 addr 2>/dev/null | grep inet; ip route 2>/dev/null
	echo "-- connman"; connmanctl state 2>&1; connmanctl technologies 2>&1 | grep -A5 -i wifi; connmanctl services 2>&1 | sed 's/wifi_[0-9a-f]*_/wifi_.../'
	ls /var/lib/connman/ 2>/dev/null | sed 's/^/config: /'
	echo "-- who is running the Wi-Fi"; pgrep -a connmand; pgrep -a wpa_supplicant; wpa_cli -i "$(ls /sys/class/net | grep ^wl | head -n 1)" status 2>/dev/null | grep -E 'wpa_state|^ssid|ip_address'
	echo "-- wireless (dmesg)"; dmesg 2>/dev/null | grep -iE 'wlan|wifi|8821|8723|brcm|cfg80211' | tail -n 20
	echo
	echo "== Wi-Fi log (Zappo X's last attempt)"; cat $D/wifi.log 2>/dev/null || echo "(none)"
	echo
	echo "== Zappo X log"; cat $D/clove.log 2>/dev/null
	echo
	echo "== Zappo X diary (runs.log)"; tail -n 60 $D/runs.log 2>/dev/null
	echo
	echo "== Zappo X settings"; cat $D/settings.json 2>/dev/null; echo
	echo
	echo "== updates"; ls -la /userdata/system/zappo-x-update 2>/dev/null || echo "(none waiting)"
	echo
	echo "== controller"; cat /proc/bus/input/devices 2>/dev/null | grep -E '^N|^H'
	echo
	echo "== processor"; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq 2>/dev/null | tr '\n' ' '; echo
	echo "== storage"; df -h /userdata /boot 2>/dev/null; grep /media /proc/mounts 2>/dev/null
	echo
	echo "== lights"; cat /var/run/knulli-rgb-led-daemon.values 2>/dev/null
	echo
	echo "== service"; /userdata/system/services/zappox status 2>/dev/null; batocera-services list 2>/dev/null | grep -i zappo
}
if [ -n "$1" ]; then out > "$1" 2>&1; else out 2>&1; fi
