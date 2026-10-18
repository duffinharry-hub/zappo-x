#!/bin/sh
# usage: shoot.sh NAME "script steps" [settings.json]
S=${CLOVE_SCRATCH:-/tmp/clove-test}
TC=$S/testcard
NAME=$1; STEPS=$2
if [ -n "$3" ]; then printf '%s' "$3" > $TC/mmc/CFW/clove/settings.json; else rm -f $TC/mmc/CFW/clove/settings.json; fi
rm -f $TC/mmc/CFW/retroarch/command.sh
CLOVE_MMC=$TC/mmc CLOVE_SD=$TC/SDCARD CLOVE_SCRIPT="$STEPS" CLOVE_LAST_SHOT=$S/${SHOTDIR:-shots}/$NAME.png timeout 60 /home/claude/clove/build/clove-host 2>&1 | grep -v "saved screenshot" | grep -v "frames presented" | grep -v starting | grep -v scanned
