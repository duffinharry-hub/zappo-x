#!/bin/sh
# Clove installer. GarlicOS runs every script in CFW/autostart at boot; this one
# only does something the first time (or after a GarlicOS update replaces CFW/main).
CFW=/mnt/mmc/CFW
[ -f "$CFW/clove/main.sh" ] || exit 0
if ! grep -q CLOVE_WRAPPER "$CFW/main" 2>/dev/null; then
	# keep GarlicOS's own launcher as main.garlic (the wrapper falls back to it)
	[ -f "$CFW/main" ] && mv -f "$CFW/main" "$CFW/main.garlic"
	cp "$CFW/clove/main.sh" "$CFW/main"
	chmod 755 "$CFW/main" "$CFW/clove/clove" 2>/dev/null
	echo "$(date) Clove installed (original launcher saved as main.garlic)" >> "$CFW/clove/install.log"
	sync
fi
exit 0
