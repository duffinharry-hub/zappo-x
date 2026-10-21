ZAPPO X DEVICE TWIN - kit for a Linux PC or VM (Ubuntu 22.04 or newer, x86-64)
==============================================================================
An emulated Anbernic RG40XX H running the real Knulli, with Zappo X on it:
the same twin the Zappo X milestones M0-M6 were tested on. Everything the
twin pretends to be (screen, pad, sticks, stick lights, battery, Wi-Fi,
Bluetooth, speaker, serial console) is described in twin/README.md.

What's in here
  Image              the twin's Linux kernel (6.18, with the twin's stand-ins)
  initrd.gz          Knulli's ramdisk plus the twin's start-up (graphics pack, Bluetooth stand-in)
  qemu.tgz           the twin's QEMU (11.1.2 with its sound patch) and its libraries - nothing to install
  twin/              the scripts: run.sh (QEMU), twinctl.py (buttons, screen, shell), selftest.py ...
  zappo-x-knulli.tar Zappo X for Knulli: the same package the PC kit puts on a card
  setup.sh           one-time: unpacks QEMU, makes the card image     start.sh       switch on
  twinctl            buttons / screenshots / shell into it            zx-install.sh  Zappo X onto it
  selftest.sh        checks every pretend part (needs python3-pil)

Set up (once) - about 15 GB free on the VM's disk
  1. Unpack Knulli's image. The two parts are on the Zappo drive:
       E:\Zappo\Zappo X\Knulli\knulli-h700-rg40xx-h-gladiator-ii-20251230.7z.001 / .002
     Copy both into the VM, then:   sudo apt install p7zip-full
                                    7z x knulli-h700-rg40xx-h-gladiator-ii-20251230.7z.001
     That gives knulli-h700-rg40xx-h-gladiator-ii-20251230.img (6 GB).
  2. sh setup.sh /path/to/knulli-h700-rg40xx-h-gladiator-ii-20251230.img
     - unpacks qemu.tgz into qemu/
     - makes run/sd.img (8 GB) from the Knulli image; the .img can go afterwards
  3. sh start.sh &            first start: a minute or two while Knulli sets the card up
     sh twinctl shot out.png  Knulli's menu should be on it
  4. sh zx-install.sh         Zappo X onto the twin; it starts straight away and at every boot
     (run it again to update: drop a newer zappo-x-knulli.tar in here first)

Every day
  sh start.sh &                 switch on          sh twinctl quit            switch off
  sh twinctl shot out.png       the screen         sh twinctl frame out.png   screen + light rings
  sh twinctl press down a       buttons: a b x y l1 r1 l2 r2 select start menu up down left right volup voldown power
  sh twinctl hold power 2000    hold a button      sh twinctl stick L 1 0     sticks (-1..1; 0 0 lets go)
  sh twinctl battery 15         battery level      sh twinctl charge on       plug the charger in / out
  sh twinctl sh 'COMMAND'       a shell inside     sh twinctl put FILE /userdata/...   copy a file in
  sh twinctl audio out.wav 10   the last 10 s of sound     sh twinctl level   how loud it is now
  sh twinctl btpad press a      the pretend Bluetooth pad  sh twinctl bt status
  sh selftest.sh                checks every part (it presses buttons: run it on Knulli's menu,
                                before step 4, or with nothing selected)
  http://127.0.0.1:18080 and :18640 reach the twin's web ports (Send to Zappo); ssh -p 18022 root@127.0.0.1
  run/console.log is the serial console; twin/README.md has the rest.

Notes
  - It is software-emulated: slower than the real handheld, and the moving backgrounds crawl.
  - There is no window: the twin's screen is read with twinctl shot/frame (PNG). A live view can
    be added later.
  - The QEMU in qemu.tgz carries its own libraries (Ubuntu 24.04's), so it runs on 22.04 too.
    It expects an x86-64 Linux; on anything else, build twin/qemu from the repo instead.
  - Source and the rest: https://github.com/duffinharry-hub/zappo-x (twin/ folder).
