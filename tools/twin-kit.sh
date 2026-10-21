#!/bin/sh
# Makes the device-twin kit for a Linux PC or VM: build/twin-kit/
#   the twin's kernel and ramdisk, its QEMU (carried with its libraries), the
#   twin scripts, the Zappo X package for Knulli (tools/knulli-package.sh), and
#   twin/kit's set-up scripts. See twin/kit/README.txt for what the user does.
#   TWIN=/home/claude/twin (kernel, QEMU build, run/initrd.gz) is where those were built.
set -e
cd "$(dirname "$0")/.."
TWIN=${TWIN:-/home/claude/twin}
K=build/twin-kit
rm -rf $K && mkdir -p $K/twin
cp "$TWIN/build/kernel/arch/arm64/boot/Image" $K/Image
cp "$TWIN/run/initrd.gz" $K/initrd.gz
cp twin/run.sh twin/twinctl.py twin/dtb.py twin/ledview.py twin/audiorec.py twin/selftest.py twin/README.md $K/twin/
cp twin/kit/README.txt twin/kit/setup.sh twin/kit/start.sh twin/kit/twinctl twin/kit/zx-install.sh twin/kit/selftest.sh $K/
[ -f build/zappo-x-knulli.tar ] || sh tools/knulli-package.sh >/dev/null
cp build/zappo-x-knulli.tar $K/
# QEMU with its libraries and the loader, so it runs on an older glibc too
Q="$TWIN/src/qemu/build/qemu-system-aarch64"
B=build/twin-kit-qemu/qemu; rm -rf build/twin-kit-qemu; mkdir -p $B/bin $B/lib
cp "$Q" $B/bin/bin-real && strip $B/bin/bin-real
ldd "$Q" | awk '/=>/ {print $3} /ld-linux/ {print $1}' | while read -r l; do [ -f "$l" ] && cp -L "$l" $B/lib/; done
for x in libgcc_s.so.1 libnss_files.so.2 libnss_dns.so.2 libresolv.so.2; do [ -f /lib/x86_64-linux-gnu/$x ] && cp -L /lib/x86_64-linux-gnu/$x $B/lib/; done
cat > $B/bin/qemu-system-aarch64 <<'W'
#!/bin/sh
# Zappo X twin: QEMU 11.1.2 with the twin's sound patch, carried with its own
# libraries (so it runs on Ubuntu 22.04 and newer without installing anything).
HERE=$(cd "$(dirname "$0")" && pwd)
exec "$HERE/../lib/ld-linux-x86-64.so.2" --library-path "$HERE/../lib" "$HERE/bin-real" "$@"
W
chmod +x $B/bin/qemu-system-aarch64 $B/bin/bin-real
(cd build/twin-kit-qemu && tar czf ../twin-kit/qemu.tgz qemu)
du -sh $K; ls $K
