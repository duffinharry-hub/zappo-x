#!/bin/sh
# Start the Zappo X device twin: an emulated RG40XX H running Knulli.
#   4 Cortex-A53 cores, 1 GB, 640x480 screen, the card in an SD slot (TF1),
#   kernel and start-up messages in console.log (like the handheld's serial
#   port), stick-light controller on a socket, a control channel for the twin's
#   helpers, network with port 18080 -> the twin's port 80 (and 18022 -> ssh), sound to a .wav.
# Everything lives in $TWIN_RUN (default /home/claude/twin/run).
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
rm -f qmp.sock led.sock agent.sock console.log twin.pid
exec "$QEMU" -name "RG40XX H twin" \
  -M virt -cpu cortex-a53 -smp 4 -m 1024 \
  -kernel "$KERNEL" -initrd "$RUN/initrd.gz" \
  -append "${TWIN_CONSOLE:-console=hvc0} loglevel=4 rootwait init=/init ${TWIN_APPEND:-}" \
  -device sdhci-pci -drive if=none,id=tf1,file="$RUN/sd.img",format=raw -device sd-card,drive=tf1 \
  -device virtio-gpu-pci,xres=640,yres=480 \
  -device virtio-serial-pci \
  -chardev file,id=con,path=console.log -device virtconsole,chardev=con,name=console \
  -chardev socket,id=led,path=led.sock,server=on,wait=off -device virtconsole,chardev=led,name=rgbled \
  -chardev socket,id=agent,path=agent.sock,server=on,wait=off -device virtserialport,chardev=agent,name=zappo.twin \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:18080-:80,hostfwd=tcp:127.0.0.1:18022-:22 -device virtio-net-pci,netdev=net0 \
  -audiodev wav,id=snd0,path="$RUN/audio.wav" -device virtio-sound-pci,audiodev=snd0,streams=1 \
  -device virtio-rng-pci \
  -display none -vga none \
  -qmp unix:qmp.sock,server=on,wait=off \
  -serial null \
  -monitor none -nodefaults -pidfile twin.pid
