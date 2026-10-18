# The Zappo X device twin

An emulated Anbernic RG40XX H for building and testing Zappo X before the hardware is in hand:
Knulli's own system image (unchanged) booting on QEMU's ARM64 `virt` machine with 4 Cortex-A53
cores, 1 GB and a 640x480 screen, the card in an SD slot (so it is `mmcblk0`, as on the handheld),
and stand-ins for the parts QEMU doesn't have:

- `kernel/` - Linux options for the twin, and `zappo_twin_power.c`, a stand-in for the AXP2202
  power chip (battery, charger, `mcu_pwr`, `moto`, `work_led` ...). Kernel code: GPL-2.0.
- `boot/` - the twin's start-up (does what Knulli's does, plus adds the twin's helpers).
- `root/` - helpers laid over Knulli at start-up: `/dev/ttyS5` for the stick-light controller,
  and a command channel for `twinctl.py`.
- `run.sh` starts it; `twinctl.py` takes screenshots and runs commands inside; `ledview.py`
  decodes what is sent to the stick lights and draws both 8-LED rings.
- `gpu/` - an ARM64 build folder matching Knulli (glibc 2.40, GCC 13 C++), a meson cross file,
  for building the twin's software graphics (Mesa) and Zappo X itself.

Knulli is not included; download the RG40XX H image from Knulli's releases.
