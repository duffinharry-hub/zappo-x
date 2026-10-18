# Zappo X on a tablet or phone (full speed)

Runs the real Zappo X natively on an Android device through **Termux**, with
the screen in Chrome: touch buttons, Bluetooth controllers, music, the stick
lights following the section, and a **Speed test** that times every picture
on the device's own processor.  Games are stand-ins (choosing one shows what
the RG40XX H would start).

First time, in Termux:

    pkg install -y git clang make
    git clone https://github.com/OWNER/zappo-x ~/zappo-x
    sh ~/zappo-x/tablet/install.sh
    sh ~/zappo-x/tablet/run.sh

After that, `sh ~/zappo-x/tablet/run.sh` starts it, and
`sh ~/zappo-x/tablet/update.sh` gets the newest version.

The test card lives in `~/zappo-card` (your settings are kept there).
`tablet/card` and `tablet/web` are built by `tools/webcard.py` and
`tools/webpage.py tablet/web --native`.
