#!/bin/sh
# Start the Zappo X device twin: an emulated RG40XX H running Knulli.
#   4 Cortex-A53 cores, 1 GB, 640x480 screen, the card in an SD slot (TF1),
#   kernel and start-up messages in console.log (like the handheld's serial
#   port), stick-light controller on a socket, a control channel for the twin's
#   helpers, network with port 18080 -> the twin's port 80 (18022 -> ssh, 18640 -> Send to Zappo), sound into a
#   pipe that audiorec.py follows (it keeps the last few minutes).
# Everything lives in $TWIN_RUN (default /home/claude/twin/run).
# Extra: TWIN_APPEND (kernel command line), TWIN_QEMU_ARGS (QEMU options, e.g. tracing),
# TWIN_SCREEN_W/H (the screen size, e.g. 1280 x 720 to see Zappo X as on a TV).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TWIN=${TWIN:-/home/claude/twin}
RUN=${TWIN_RUN:-$TWIN/run}
QEMU=${QEMU:-$TWIN/src/qemu/build/qemu-system-aarch64}
KERNEL=${KERNEL:-$TWIN/build/kernel/arch/arm64/boot/Image}
cd "$RUN"
if [ -f twin.pid ] && kill -0 "$(cat twin.pid)" 2>/dev/null; then
  echo "the twin is already running (pid $(cat twin.pid))" >&2; exit 1
fi
rm -f qmp.sock led.sock agent.sock audio.sock console.log twin.pid led-state.json
[ -p audio.fifo ] || { rm -f audio.fifo; mkfifo audio.fifo; }
# the device tree: QEMU's own, with the handheld's model name (dtb.py)
DTB="$RUN/rg40xxh-$(stat -c %Y "$QEMU").dtb"
if [ ! -f "$DTB" ]; then
  rm -f "$RUN"/rg40xxh-*.dtb
  "$QEMU" -M virt,dumpdtb="$RUN/virt.dtb" -cpu cortex-a53 -smp 4 -m 1024 -nodefaults -display none >/dev/null 2>&1
  python3 "$HERE/dtb.py" "$RUN/virt.dtb" "$DTB" && rm -f "$RUN/virt.dtb"
fi
# the stick lights: decoded from the light channel as they change (ledview.py)
python3 "$HERE/ledview.py" record >/dev/null 2>&1 &
# the speaker (QEMU waits for this to open the pipe)
python3 "$HERE/audiorec.py" record >/dev/null 2>&1 &
exec "$QEMU" -name "RG40XX H twin" \
  -M virt -cpu cortex-a53 -smp 4 -m 1024 \
  -kernel "$KERNEL" -initrd "$RUN/initrd.gz" -dtb "$DTB" \
  -append "${TWIN_CONSOLE:-console=hvc0} loglevel=4 rootwait init=/init mac80211_hwsim.radios=4 ${TWIN_APPEND:-}" \
  -device sdhci-pci -drive if=none,id=tf1,file="$RUN/sd.img",format=raw -device sd-card,drive=tf1 \
  -device virtio-gpu-pci,xres=${TWIN_SCREEN_W:-640},yres=${TWIN_SCREEN_H:-480} \
  -device virtio-serial-pci \
  -chardev file,id=con,path=console.log -device virtconsole,chardev=con,name=console \
  -chardev socket,id=led,path=led.sock,server=on,wait=off -device virtconsole,chardev=led,name=rgbled \
  -chardev socket,id=agent,path=agent.sock,server=on,wait=off -device virtserialport,chardev=agent,name=zappo.twin \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-:80,hostfwd=tcp:127.0.0.1:18022-:22,hostfwd=tcp:127.0.0.1:18640-:8640 -device virtio-net-pci,netdev=net0 \
  -audiodev wav,id=snd0,path="$RUN/audio.fifo" -device virtio-sound-pci,audiodev=snd0,streams=1 \
  -device virtio-rng-pci \
  -display none -vga none \
  -qmp unix:qmp.sock,server=on,wait=off \
  -serial null \
  -monitor none -nodefaults -pidfile twin.pid ${TWIN_QEMU_ARGS:-}
