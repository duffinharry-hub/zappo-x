#!/bin/sh
# Checks every pretend part of the running twin (needs python3-pil: apt install python3-pil)
HERE=$(cd "$(dirname "$0")" && pwd)
TWIN_RUN="$HERE/run" exec python3 "$HERE/twin/selftest.py" "$@"
