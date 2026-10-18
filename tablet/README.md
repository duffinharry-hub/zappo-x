# Zappo X on a tablet or phone (full speed)

Runs the real Zappo X natively on an Android device through **Termux**, with
the screen in Chrome: touch buttons, Bluetooth controllers, music, the stick
lights following the section, the **moving backgrounds**, **Send to Zappo**,
**Get box art**, and a **Speed test** that times every picture on the
device's own processor.  **Games really play**: choosing one starts it in
RetroArch for Android (the same emulator cores Knulli uses, built for
Android), and quitting RetroArch brings you back to Zappo X. Films open in
the tablet's video player.

First time, in Termux:

    pkg install -y git clang make
    git clone https://github.com/duffinharry-hub/zappo-x ~/zappo-x
    sh ~/zappo-x/tablet/install.sh
    sh ~/zappo-x/tablet/run.sh

`sh ~/zappo-x/tablet/update.sh` gets the newest version (and adds anything new
it needs, such as Python).

## Zappo X as an app

The install keeps Zappo X running in the background (Termux starts it each
time Termux opens). The first time `run.sh` opens it in Chrome, tap
**Install app** at the top (or Chrome's menu > **Add to home screen** >
Install). From then on the **Zappo X icon** on the home screen opens it like
any app: full screen, sideways, no Termux, no address bar.

- After the tablet restarts, open Termux once (it starts Zappo X), or install
  **Termux:Boot** from F-Droid and open it once: then Zappo X starts with the
  tablet by itself.
- If Zappo X isn't running, the app says so and connects as soon as it is.
- Samsung may stop Termux in the background to save battery: Android's
  Settings > Apps > Termux > Battery > **Unrestricted** keeps it going.
- **Termux:Widget** (F-Droid) can put a "Zappo X" shortcut on the home screen
  that starts it and opens it, if you prefer that.
- `sh ~/zappo-x/tablet/server.sh stop` stops it; `... status` says whether
  it's running.

## Controllers

A Bluetooth controller (Xbox, PlayStation, 8BitDo ...) works in Zappo X and
in the games. In Zappo X its buttons go by what's printed on them: **A**
confirms, **B** goes back (Settings > Controls > Confirm button swaps them).
As soon as the controller is used, the on-screen buttons go away and the
screen fills the tablet; touch the screen to bring them back.
In games, RetroArch places buttons by position (as on a Nintendo pad); if A
and B feel swapped in a game, swap them in RetroArch: Quick Menu > Controls.

## Playing games

1. **RetroArch for Android**: get the 64-bit version from retroarch.com
   (Download > Android). Open it once, allow it to see your files, and quit.
   (The Play Store and F-Droid versions work too; retroarch.com's has the
   core downloader built in.)
2. **Let Termux open RetroArch**: Android's Settings > Apps > Termux >
   **Appear on top** > Allow. (Android only lets an app in the background open
   another app with this.)
3. **Games**: the install puts Zappo X's card in the tablet's own storage, in
   a folder called **Zappo X**. Games go in `Zappo X/mmc/Roms/<console>` -
   the GarlicOS folder names: `GB`, `GBC`, `GBA`, `FC` (NES), `SFC` (SNES),
   `MD`, `MS`, `GG`, `PS`, `PCE`, `NGP`, `WS`, `NDS`, `N64`, `PSP` ... Copy
   them from a PC over USB (the tablet shows up as a drive), or use **Send to
   Zappo** (below) and Zappo sorts them.
4. **Cores**: the first time a console's game starts, RetroArch may say it
   can't load the core. In RetroArch: Load Core > Download a Core, pick the
   one Zappo's screen named (mgba, snes9x, gambatte ...), quit, and start the
   game again from Zappo X.

Quitting RetroArch (its menu > Quit) comes back to Zappo X by itself. Saves
are RetroArch's own (in the tablet's `RetroArch` folder). PlayStation and a
few others want BIOS files: RetroArch looks in `RetroArch/system`.

## Send to Zappo

Settings > Send to Zappo shows an address and a QR code. On a phone or PC on
the **same Wi-Fi**, scan the code (or type the address): a page opens where
you choose or drop games, songs, albums and films. They go into the card's
Import folder, and Zappo sorts them when you leave the Send to Zappo screen.
A whole folder of games works too (for several consoles at once): each game
goes to its own console. A game in folders of its own ("Disc 1", "Disc 2")
or a .cue with its .bin files stays together. Anything Zappo can't place
(a film in a .zip, say) waits under **Import** in the console list, where
you choose the console.
The page only runs while that screen is open. It uses the port after Zappo's
own (8641), so the tablet's screen page on 8640 keeps working.

## Get box art

Zappo looks for box art by itself whenever new games are sorted (and once
for the games already there, the first time this version starts).
Settings > Games > Get box art (or a console's own settings, for just that
console) does it any time. Pictures come from the libretro thumbnail library
(the pictures RetroArch uses), over Wi-Fi, for games that have none, and are
saved as `Roms/<console>/Imgs/<game>.png`, as on a GarlicOS card. It carries
on in the background; the menu shows how it's going, and Zappo says when
it's done. Games with home-made names (a ROM hack, "Pokemon Unbound") may not
be in the library.

The console list shows a picture of each console: RetroArch's "Systematic"
icons (CC BY 4.0, see `tablet/icons`).

## Smoothness: Tablet power

Zappo X draws its menus as on the handheld, and the tablet's graphics chip
puts them on screen (WebGL in the app): the background picture, the moving
particles and the menu as separate layers, as the RG40XX H's GPU does. Only
what changed is sent, and a slow moment skips pictures instead of falling
behind.

Settings > Performance > **Tablet power**:
- **Balanced** (to start with): the moving backgrounds at 30 frames a second.
- **Full**: everything at 60 frames a second.

Settings > Look > Moving backgrounds: Off, Fewer or On. Without WebGL the
picture is put together on the processor instead (slower).

## Where things are

The card is the **Zappo X** folder in the tablet's own storage
(`/storage/emulated/0/Zappo X`); your settings are kept there, and Send to
Zappo and Get box art run from its `mmc/CFW/clove`. The first install that
puts it there brings your settings across from the old `~/zappo-card` and
leaves that folder as it was. If Termux isn't allowed to see your files, the
card stays in `~/zappo-card` with the pretend games, and games can't start in
RetroArch.
`tablet/card` and `tablet/web` are built by `tools/webcard.py` and
`tools/webpage.py tablet/web --native`.
