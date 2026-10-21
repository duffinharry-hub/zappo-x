#!/bin/sh
# Switch the twin on (in the background). Screen: sh shot.sh ; buttons: sh press.sh a
HERE=$(cd "$(dirname "$0")" && pwd)
export TWIN="$HERE" TWIN_RUN="$HERE/run" QEMU="$HERE/qemu/bin/qemu-system-aarch64" KERNEL="$HERE/Image"
exec sh "$HERE/twin/run.sh" "$@"
