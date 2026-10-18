#!/bin/sh
# Puts a fresh Knulli build of Zappo X (program, start loop, service) into the
# running twin and restarts it.
#   twin/zx-update.sh          (after: make knulli)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
R=$HERE/..
T="python3 $HERE/twinctl.py"
$T put "$R/build/zappo-x" /userdata/system/zappo-x/zappo-x.new >/dev/null
$T put "$R/knulli/zappo-x.sh" /userdata/system/zappo-x/zappo-x.sh.new >/dev/null
$T put "$R/knulli/zappox" /userdata/system/services/zappox.new >/dev/null
$T sh '/userdata/system/services/zappox stop; killall zappo-x.sh 2>/dev/null; sleep 1
cd /userdata/system/zappo-x && mv -f zappo-x.new zappo-x && mv -f zappo-x.sh.new zappo-x.sh && chmod +x zappo-x zappo-x.sh
mv -f /userdata/system/services/zappox.new /userdata/system/services/zappox && chmod +x /userdata/system/services/zappox
/userdata/system/services/zappox start; sleep 2; /userdata/system/services/zappox status' 120
