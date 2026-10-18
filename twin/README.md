# The Zappo X device twin

An Anbernic RG40XX H that runs on a computer: the real Knulli system (the
RG40XX H build of 30 Dec 2025) on an emulated ARM machine set up like the
handheld, so Zappo X can be built and tested before the handheld arrives.

| Part | On the RG40XX H | On the twin |
|---|---|---|
| Processor, memory | Allwinner H700, 4 × Cortex-A53, 1 GB | QEMU `virt`, 4 × Cortex-A53, 1 GB |
| Model name | Anbernic RG40XX H | the same, in the device tree (`dtb.py`) |
| Screen | 640×480 | virtio-gpu 640×480 |
| Graphics | Mali G31 (libmali) | Mesa softpipe, SDL 2.30.12 on KMS, a libmali stand-in (`gpu/`) |
| Card | TF1 | an SD card slot holding the Knulli card image |
| Buttons, sticks | "Anbernic RG40XX-H Controller" | a gamepad with the same name, ids and codes (`zappo-twin-agent`) |
| Stick lights | controller on /dev/ttyS5 | the same bytes, read on a socket and drawn as two rings (`ledview.py`) |
| Battery, charger | AXP2202 | a kernel stand-in with the same names (`kernel/zappo_twin_power.c`) |
| Wi-Fi | Realtek radio | a simulated radio, and a second one running the "ZappoTwin" network (`S06twinwifi`) |
| Bluetooth | Realtek chip on ttyS1 (`rtk_hciattach`) | a virtual controller with a pad, headphones and a phone in range (`bt/`) |
| Sound | the handheld's speaker | virtio sound, kept by `audiorec.py` |

What runs on the twin is Knulli's own: its start-up, its settings, its Wi-Fi
and Bluetooth tools, its light daemon. Only the hardware underneath is
pretend, and everything pretend is in this folder; none of it ships on a real
handheld.

## Using it

```sh
twin/run.sh &                     # switch it on (about a minute to Knulli's menu)
twin/selftest.py                  # check every part: model, screen, pad, lights,
                                  # battery, wifi, sound, bt - one line each
twin/twinctl.py shot out.png      # the screen
twin/twinctl.py frame out.png     # the whole handheld: screen and both light rings
twin/twinctl.py press down a      # buttons (a b x y l1 r1 l2 r2 select start menu
                                  # up down left right volup voldown power)
twin/twinctl.py stick L 1 0       # sticks, -1..1
twin/twinctl.py battery 12        # battery level; charge on|off for the charger
twin/twinctl.py sh 'COMMAND'      # a shell command inside
twin/twinctl.py audio out.wav 10  # the last 10 seconds it played
twin/twinctl.py bt status         # the pretend Bluetooth devices
twin/twinctl.py btpad press a     # the Bluetooth pad's buttons, once paired
twin/twinctl.py quit              # switch it off
```

From outside, `127.0.0.1:18080` and `:18022` reach the twin's ports 80 and 22
(through its Wi-Fi, as a phone on the same network would).

## The pretend devices

- **ZappoTwin** (Wi-Fi, password `zappo1234`): the twin's router. The handheld
  always gets 192.168.77.20.
- **Zappo Twin Pad** (Bluetooth): pairs with Knulli's own "pair a controller",
  then is a real input device in the twin. `btpad wake` makes it reconnect by
  itself, as a real pad does when switched on. Its pairing survives a restart.
- **Zappo Twin Buds** (Bluetooth): headphones that pair but play nothing; the
  twin has no Bluetooth audio.
- **Zappo Twin Phone** (Bluetooth): shows up in scans, like the phones around a
  real handheld.

## Building

The pieces are built once and laid over Knulli at start-up by `boot/mkinitrd.py`:

- `kernel/`: Linux 6.18 with `twin.config`, the power-chip stand-in and the
  virtio-gpu patch.
- `qemu/`: a QEMU 11.1.2 patch so the sound device keeps working past the first
  sound.
- `gpu/build.sh`: the graphics pack (needs `gpu/mksysroot.py`'s sysroot).
- `bt/build.sh`: the Bluetooth stand-in, from BlueZ 5.79's controller emulator.
- `root/`: the twin's own files (start-up scripts, the helper, stand-ins for
  `brightness` and `rtk_hciattach`).

## What the twin can't tell us

These are guesses until the real handheld is here:

- How fast the light controller breathes and cycles, and where LED 0 sits on
  each ring.
- What the three lowest button codes (288-290) are on the real controller.
- The real model string in the handheld's device tree.
- Real timings: the twin's processor is emulated, so it is far slower than an
  H700. Speed is measured on the tablet test bench, and on the handheld.
- Real radios: range, the shared Wi-Fi/Bluetooth antenna, Bluetooth audio.
