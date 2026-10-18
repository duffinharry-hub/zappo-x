# Zappo X on a tablet or phone (full speed)

Runs the real Zappo X natively on an Android device through **Termux**, with
the screen in Chrome: touch buttons, Bluetooth controllers, music, the stick
lights following the section, the **moving backgrounds**, **Send to Zappo**,
**Get box art**, and a **Speed test** that times every picture on the
device's own processor.  Games are stand-ins (choosing one shows what the
RG40XX H would start).

First time, in Termux:

    pkg install -y git clang make
    git clone https://github.com/duffinharry-hub/zappo-x ~/zappo-x
    sh ~/zappo-x/tablet/install.sh
    sh ~/zappo-x/tablet/run.sh

After that, `sh ~/zappo-x/tablet/run.sh` starts it, and
`sh ~/zappo-x/tablet/update.sh` gets the newest version (and adds anything new
it needs, such as Python).

## Send to Zappo

Settings > Send to Zappo shows an address and a QR code. On a phone or PC on
the **same Wi-Fi**, scan the code (or type the address): a page opens where
you choose or drop games, songs, albums and films. They go into the card's
Import folder, and Zappo sorts them when you leave the Send to Zappo screen.
The page only runs while that screen is open. It uses the port after Zappo's
own (8641), so the tablet's screen page on 8640 keeps working.

## Get box art

Settings > Games > Get box art (or a console's own settings, for just that
console) finds pictures for games that have none, from the libretro thumbnail
library (the pictures RetroArch uses), over Wi-Fi. They are saved as
`Roms/<console>/Imgs/<game>.png` on the test card, as on a GarlicOS card.
It carries on in the background; the menu shows how it's going.

## Moving backgrounds

Settings > Look > Moving backgrounds: each section's Zappo element drifts
behind the menus (Off, Fewer, On). On the tablet the picture is put together
on the processor and streamed to the page, about 20 frames a second.

## Where things are

The test card lives in `~/zappo-card` (your settings are kept there; Send to
Zappo and Get box art run from `~/zappo-card/mmc/CFW/clove`).
`tablet/card` and `tablet/web` are built by `tools/webcard.py` and
`tools/webpage.py tablet/web --native`.
