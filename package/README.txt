ZAPPO 0.3.5 - a faster, customisable menu for GarlicOS (built as "Clove")
Anbernic RG35XX (original model, GarlicOS 1.x)
Created by CovGNG
=====================================================================

Zappo (the launcher formerly called Clove; its folders on the card are
still named CFW/clove) replaces the GarlicOS menu. GarlicOS's own system, RetroArch and
emulators stay exactly as they are; Clove only changes the menu and how
games are started:

  * Each console runs at a fixed CPU speed while you play (GarlicOS lets
    the clock speed up and slow down mid-game, a common cause of stutter).
  * Per-console emulator, CPU speed, scaling, threaded video, audio
    latency and FPS counter.
  * The Zappo family: Zappo and his elemental cousins (fire, leaf, crystal,
    ice and water) keep you company on the Recent and Favorites shelves and
    the empty screens, each has a lock screen picture, and there's a theme
    for each (Zappo Electric, Fire, Leaf, Crystal, Ice and Water) with his
    picture sharp behind every screen, Classic or Glass. The lock screen
    stays the group picture, Zappo Friends; change it in Settings > Screen &
    power > Lock screen picture.
  * Themes (Zappo, Clove, Lagoon, six Swirls, or make your own), console and
    game-list layouts (Carousel, Grid, List, Shelf), box art, favourites, recents.
  * Your GarlicOS favourites, box art (Roms/<system>/Imgs) and skin keep
    working.
  * Every console GarlicOS has an emulator for is listed, the ones with
    games first. Consoles that need a BIOS file say which one is missing.
  * An Import folder: drop games in from your PC and Clove sorts them into
    the right console folders by itself.
  * A Music tab with Artists, Genres, Songs and Folders, cover art and a
    full-screen Now Playing view where you can skip through a song. Plays
    MP3, FLAC, OGG and WAV and keeps playing while you browse.
  * A Videos tab: your films and clips (MP4, MKV, AVI ...) as a wall of
    pictures or a list, with running times and a tick on the ones you've
    watched. MP4s that the video player can't open straight away are got
    ready the first time you play them.
  * A lock screen (L1 + R1 to unlock, with song skipping and your own
    pictures to choose from), a screensaver with a big clock, and 7 themes with
    their own wallpapers: Lagoon and six glossy "Swirl" colours (Green,
    Amber, Blue, Violet, Gold, Graphite).
  * Two new looks: Glass (your wallpaper softly blurred behind every
    screen, see-through panels) and the Shelf layout (big box art in the
    middle, the background tinted to match it).
  * Playlists (plus "Recently added" and "Most played"), a sleep timer,
    even volume between songs, and music that picks up where it was after
    a game.
  * Search: type a few letters to find any game, artist, song or playlist.
  * Games carry on where you left off (or start over, from X).
  * Battery time left and a low-battery warning; a screenshot viewer.
  * Adjustable button sensitivity (how fast held buttons scroll).
  * Includes BLOCKBOX, a small Minecraft-style building game.

If anything misbehaves, Clove falls back to the normal GarlicOS menu by
itself (see "If something goes wrong").


INSTALL
-------
1. Back up your GarlicOS SD card (the one in slot TF1). Win32 Disk Imager
   or balenaEtcher can save a full image of it.
2. Put the card in your computer and open its ROMS partition (the big one
   with the CFW, BIOS and Roms folders).
3. Windows: run Zappo.bat from the download and pick 1. It copies Zappo
   on, fixes the video player and (when the small boot partition shows up
   too) puts on the boot logo and charging animation - all undoable from
   the same menu.
   Any other computer: copy everything inside the download's "card" folder
   onto the partition. Choose "merge"/"replace" when asked; nothing of
   yours is deleted. (The video player fix and the boot pictures need the
   Windows kit.)
4. Put the card back and turn the RG35XX on.

On the first boot Clove installs itself before the menu appears: it
renames GarlicOS's launcher CFW/main to CFW/main.garlic and puts a small
script in its place, so you go straight into Clove. (If a GarlicOS update
later replaces CFW/main, Clove re-installs itself the same way.)

Games on a second card in TF2 work as before (Roms/<system> folders on
either card are merged).


ADDING GAMES: THE IMPORT FOLDER
-------------------------------
Copy games, music and BIOS files into the Import folder at the top of the
card (the partition with the CFW, BIOS and Roms folders). When Clove
opens, it moves each one into the right Roms/<console> folder:

  * by file type: .gba .gbc .gb .nes .sfc .smc .md .sms .gg .pce .ngp .ws
    .lnx .a26 .32x .vb .p8.png and many more,
  * PlayStation discs (.cue + .bin, .chd, .pbp, .m3u), PC Engine CD and
    Sega CD discs by looking inside them,
  * .zip files by what's inside them, arcade .zip sets by their MAME name,
  * BIOS files (scph1001.bin, gba_bios.bin, disksys.rom, neogeo.zip ...)
    into the BIOS folder,
  * a folder named after a console (GBA, SNES, PS1, Mega Drive, Arcade ...)
    has its contents moved into that console,
  * songs (.mp3 .flac .ogg .wav .m4a .aac .opus ...) and album folders
    into the Music folder. An .mp4 with a picture is a video and goes to
    Videos instead.

Anything Clove can't place stays in Import and shows up as "Import" at the
top of the Consoles list; open it and pick the console for each item.
Games can still be copied straight into Roms/<console> as before.


BUTTONS IN THE MENU
-------------------
  D-pad          move
  A              open / play            (swap A and B in Settings > Controls)
  B              back
  L1 / R1        switch tabs (Recent, Favorites, Consoles, Music, Videos,
                 RetroArch, Settings)
                 inside a console or a series folder: page up / down
  L2 / R2        jump to the previous / next letter (any A-Z list: games,
                 films, artists, songs)
  Y              favourite: games, films and songs (songs go in the
                 Favorites playlist)
  X              options for what's selected (games: start over, favourite,
                 remove from recent, console settings; on the Consoles
                 tab: search, that console's settings, surprise me)
  SELECT         switch layout (consoles, game list, music or videos)
  START          settings (in a game list: that console's settings;
                 on the Music tab while a song plays: Now Playing)
  MENU           quick menu, from anywhere: Now Playing and pause / play
                 while music is on, battery time left, search, screenshots,
                 settings, lock screen, power off, reboot

The same buttons do the same jobs on every screen, and the two players
(music and video) skip the same way: LEFT / RIGHT 10 seconds, L1 / R1 a
minute; your confirm button pauses, your back button leaves.
  POWER          lock screen / power off / reboot
  L1 + R1        (together) unlock the lock screen
  VOLUME + / -   brightness (music volume while a song is playing)

In games, GarlicOS's RetroArch controls are unchanged.

Settings > Controls > Sensitivity: how long a button is held before it
repeats, how fast lists scroll, and whether scrolling speeds up when held.


MUSIC
-----
Put songs in the Music folder at the top of the card (or drop them into
Import). SELECT switches the Music tab between:

  Artists   every artist, with their genre, number of songs, albums and
            total time; open one for their songs, "Play all" or "Shuffle"
  Genres    the styles of music; open one for its artists
  Songs     every song A-Z
  Playlists your playlists, plus "Recently added" (the newest songs on
            the card) and "Most played"
  Folders   the Music folder as it is on the card

  A              open / play / pause
  B              back
  START          Now Playing: the cover, big, with what's playing
  L2 / R2        jump to the previous / next letter
  X              options: play or shuffle all of an artist / genre /
                 playlist, add to a playlist, remove from this playlist,
                 set an artist's genre, sleep timer, stop, shuffle,
                 repeat, read the Music folder again
  Y              on a song: add it to (or take it out of) Favorites
  VOLUME + / -   music volume

On Now Playing:
  A              pause / play            B   back to the list
  LEFT / RIGHT   skip back / forward 10 seconds (hold to keep skipping)
  L1 / R1        skip back / forward 1 minute
  L2 / R2        previous / next song
  Y              add this song to Favorites (again: take it out)
  SELECT         shuffle on / off (repeat is under X)
  X              add this song to a playlist, sleep timer, repeat ...

MENU, from any screen, has Now Playing and pause / play while music is on.

Playlists: Playlists > New playlist, type a name, then press X on any
song, artist or genre > "Add to a playlist". They are ordinary .m3u files
in Music/Playlists, so a PC music player can make or play them too. A
song counts as played (for Most played) once 30 seconds of it have
played.

Sleep timer (X > Sleep timer): 15, 30, 45 minutes, 1 hour, 1 hour 30, or
at the end of the song. The music fades out over the last 10 seconds and
pauses; the time left shows on Now Playing and the lock screen.

Settings > Music:
  Volume levelling       evens out loud and quiet songs. Each song is
                         measured as it plays (CFW/clove/loudness.tsv)
                         and starts at the right level from then on.
  Remember where I was   after a game or a restart, the song you were
                         playing is waiting, paused at the same spot:
                         Music tab > START > A.

Names: Clove reads the song's tags. Songs without tags (most "video to
MP3" downloads) are named from the file name, tidied up:
"Artist - Song (Official Video) - Channel.mp3" shows as "Song" by
"Artist". If one still comes out wrong, put a line in Music/songs.txt:
    file name.mp3 = Artist | Title

Genres: Music/genres.txt has one line per artist ("Artist = Genre"); the
handheld writes it when you use X > Set genre. Songs whose tags carry a
genre use that unless genres.txt says otherwise.

Pictures: a picture with the song's name next to it (Song.jpg), else
Music/Covers/<song file name>.jpg, else the cover inside the file, else
the folder's cover.jpg, else Music/Artists/<Artist>.jpg. Without any,
Clove draws a cover in the artist's own colours.

Cover art from the internet: on a PC, Zappo.bat -> 4 (in the Zappo kit)
with the card in. It looks every song up on
Deezer and iTunes and saves the covers into Music/Covers and pictures of
the artists into Music/Artists. Songs it can't match for certain are left
alone. After adding music, start the handheld and open the Music tab once
(Clove then lists the new songs in CFW/clove/music-list.tsv), then run
it again; it only fetches what's missing.

MP3, FLAC, OGG and WAV play inside Clove; music stops when a game starts.
Other formats (M4A, AAC, OPUS, WMA ...) open in RetroArch's media player.


VIDEOS
------
Put videos in Roms/VIDEOS (or drop them in Import); folders inside it
(say, one per series) show up as folders. The Videos tab shows them as a
wall of pictures or a list (SELECT switches), with each film's running
time and a tick on the ones you've watched (X: mark as watched or not,
player buttons, video settings; Y: favourite; L1 / R1 inside a folder:
page up / down). The pictures come from the
PC kit: Zappo.bat -> 4 finds a poster for each film (or, failing that,
takes a frame from it) and saves it as Roms/VIDEOS/Imgs/<file name>.png
(put your own poster there if you prefer). Settings > Home screen > Tabs hides
the tab, and Videos goes back to being a console on the Consoles tab.

Inside a series folder, when an episode plays to the end the next one starts
by itself ("up next"; your back button leaves as usual, and each one counts
as watched). On
the Videos tab, X > "Continue <series>" jumps straight to the next unwatched
episode of the series you were last watching. Films never resume at
switch-on the way games do.

Videos play in RetroArch's FFmpeg player (the buttons are shown while it
opens):

  LEFT / RIGHT   skip back / forward 10 seconds
  L1 / R1        skip back / forward 1 minute (UP / DOWN too)
  A              pause / play
  B              back to Zappo (closes the film, then the player)
  MENU           RetroArch's menu
(the same as the music player; with A and B swapped in Settings > Controls,
B pauses and A goes back here too)

Clove sets these buttons up each time a video starts (in RetroArch's
config/FFmpeg/FFmpeg.cfg and config/remaps/FFmpeg/FFmpeg.rmp); games keep
GarlicOS's usual controls. Clove also comes with a fixed copy of the
FFmpeg player: GarlicOS's own one closed whenever you skipped.

Getting films onto the card from a PC: drop them in Downloads (or
Downloads\videos; a folder in there becomes a folder on the Videos tab)
and run Zappo.bat -> 2. Zipped collections are unpacked by Zappo.bat -> 3
first. Anything HD (over 480p), over the
player's 2 GB limit, or with several sound tracks is shrunk by the kit
with ffmpeg (fetched once into tools\ffmpeg): 640 pixels wide, English
sound only, ready to play straight away. tools\video-names.txt gives a
file a nicer name on the card, or "skip" leaves it out.

If you'd rather do it yourself, e.g. with HandBrake: H.264, about 640
pixels wide, "Fast Decode" ticked, and tick "Web Optimized" (Summary tab).
Files of 2 GB or more can't be opened by the player.

If an MP4 isn't "web optimized", the first time you play it Clove moves
the film's index to the front of the file (a progress bar shows how long
is left; a full-length film takes a few minutes, B stops it). Nothing is
re-encoded and the picture doesn't change. After that it starts straight
away.


SEARCH
------
X on the Consoles tab (or MENU > Search). Type with the on-screen
keyboard: the D-pad moves, A types, Y is a space, X deletes, B deletes
(or goes back when there's nothing typed). Results appear as you type:
games from every console, artists, songs and playlists. Press START (or
up) to go into the results, A to play or open one.


CARRY ON WHERE YOU LEFT OFF
---------------------------
When you close a game, RetroArch saves exactly where you were, and the
next time you start it, it carries on from there. A game with a saved
spot says "Continues where you left off" next to its box art. To play
from the beginning instead: X > Start over. Turn it off in
Settings > Games > Continue where you left off. (Arcade games always
start fresh: GarlicOS sets them up that way.)


BOX ART FOR YOUR GAMES
----------------------
On a PC, Zappo.bat -> 4 (in the Zappo kit) looks your games
up in the libretro picture library (the pictures RetroArch itself uses)
and saves the covers into Roms/<console>/Imgs. Games it doesn't know
(ROM hacks, homebrew) are looked up on TheGamesDB instead. If a file name
isn't the game's name, tell it what to search for in
tools/game-art-names.txt ("file name = Pokemon Radical Red"). Anything
still without a picture keeps Clove's own tile; put your own there as
Roms/<console>/Imgs/<game file name>.png.


BATTERY AND SCREENSHOTS
-----------------------
On the charger the quick menu says how long until full (measured after the
first ten minutes plugged in); Settings > Screen & power also says how long
a full charge lasts the way the handheld has been used lately. Once a week
Zappo copies the saves and states to Backups\saves-<date> at the top of the
card (the newest three are kept). If the handheld ever restarts on its own
while something runs, Zappo says so quietly at the next start and keeps the
logs in CFW/clove.

Clove notes the battery level now and then and works out how long is left
from how fast it has been going down since the last charge (games
included). It shows on the lock screen and at the top of the MENU button's
menu, and Clove warns you at 15% and 5%. Until it has measured a full
stretch it makes a first guess. Turn it off in Settings > Screen & power.

Screenshots: in a game, hold MENU and press UP (RetroArch's screenshot
button). MENU > Screenshots shows them, newest first: LEFT / RIGHT to
browse, L1 / R1 to jump 10, A to show or hide the name and date, B back.


SCREENSAVER, CLOCK AND LOCK SCREEN
----------------------------------
After 2 minutes without a button press Clove shows the Lagoon wallpaper
with a big clock, the date and the song that's playing; any button brings
you back (that press does nothing else).

After 10 minutes (or POWER > Lock screen, or MENU > Lock screen) Clove
locks: the same clock and wallpaper on a very dim screen, with the music
still playing. Press L1 and R1 together to unlock. While locked:

  LEFT / RIGHT   previous / next song (also L2 / R2)
  A              pause / play the song
  VOLUME + / -   music volume
  POWER          straight back to dim
  anything else  lights the screen up for a few seconds

Clock font: Settings > Screen & power > Clock font picks the typeface of
the big clock (8 included in CFW/clove/fonts: Outfit, Big Shoulders, Jura,
Pixelify, JetBrains Mono, Lora, Tektur, Poiret One; add your own .ttf).

Settings > Screen & power sets how long before the screensaver and before
it locks, whether the lock screen shows the clock or turns the screen off
(saves the most battery), and the lock screen picture: the theme's own,
one of those in CFW/clove/lockscreens (Zappo Meadow and Zappo Friends
come with it) or Shuffle for a different one each time. Press A on it to
preview. Add your own 640 x 480 .jpg or .png pictures to that folder and
they appear in the list too.

The RG35XX has no internet time: set the time and date once in
Settings > Home screen > Set clock.

Each Swirl theme brings its own screensaver wallpaper; pick one in
Settings > Look > Theme. To use your own picture, put a 640 x 480
wallpaper.png in the theme's folder (or replace CFW/clove/wallpaper.png,
which the other themes use).


LOOKS: GLASS AND SHELF
----------------------
Settings > Look > Style:
  Classic   the theme's colours, as before
  Glass     the lock screen picture (or the theme's wallpaper), softly
            blurred and darkened, behind every screen; see-through panels
            and a slim tab bar of icons. Change the picture in
            Settings > Screen & power > Lock screen picture.

Music layout (X > Layout on the Music tab): List with details, a Grid of
covers, or a Shelf. MENU pauses and resumes the music from any screen.

Layouts (SELECT on the console list or a game list switches them, or
Settings > Look): Shelf puts the console or the game's box art big
in the middle with its neighbours either side, and tints the background
with its colours. LEFT / RIGHT move one, UP / DOWN (or L1 / R1) five.
Carousel, Grid, List and List with art are still there.

0.3.5 switches Glass and Shelf on once; change them back any time.


MAKING IT YOURS
---------------
Everything is in Settings on the device. For deeper changes, edit these
files on the SD card (CFW/clove/):

  wallpaper.png              the screensaver picture (640 x 480)
  themes/<Name>/theme.json   colours, font, sizes, corner radius, layouts,
                             background image. See THEMES.txt.
                             "GarlicOS skin" uses your CFW/skin files.
  settings.json              written by Clove. You can add a custom name
                             for any console:
                               "systems": { "PS": { "name": "PS1" } }
  systems.json               add your own console folders, e.g.
                               { "MSU1": { "name": "SNES MSU-1",
                                           "core": "snes9x_libretro.so",
                                           "cpu": 1488000 } }
                             (see systems.json.example)
  input.json                 button mapping, if a button does nothing.
                             Settings > Controls > Input test shows what
                             each button sends. See input.json.example.
  icons/<FOLDER>.png         console artwork used when your skin has none.

Boot and charging screens: the update puts Zappo's boot logo, charging
screen and charging animation on the TF1 card. The animation lives inside
the system's ramdisk.img, so the kit keeps the original in
TF1-boot-partition-original (copy it back onto the small boot partition
to undo), and only replaces GarlicOS's own ramdisk, never one from
somewhere else.

CPU speeds: Settings > Games > Consoles > (console) > CPU speed. "Recommended"
picks a speed per console (1.0 GHz for Game Boy/NES up to 1.5 GHz for
PlayStation and arcade). Clove never goes above 1.5 GHz, the top speed
GarlicOS itself uses. Higher speeds use more battery.

What Clove changes in RetroArch's settings: like GarlicOS, it points the
BIOS/saves/states folders at the card your game is on. If you change a
console's scaling, threaded video, audio latency or FPS counter in Clove,
those settings are applied for that console only. For consoles with a
fixed CPU speed, Clove also sets RetroArch's "CPU scaling" to Manual so
RetroArch doesn't switch the clock back to automatic mid-game; consoles
set to "GarlicOS automatic" get your normal RetroArch setting back.
Everything else stays as you set it in RetroArch.


BLOCKBOX
--------
Consoles > Blockbox. Each "World" file is its own world and saves itself
when you quit (RetroArch save file). Copy "World 1.world" and rename it to
make more worlds; the name decides the landscape.

  D-pad up/down     walk            D-pad left/right   turn
  L1 / R1           step sideways   L2 / R2            look up / down
  B                 jump            Y                  break block
  A                 place block     X                  next block
  START             menu: flying, detail, time of day, coordinates,
                    sensitivity (turn / look / move speed, invert look),
                    controls, new world
  SELECT            fly down (when flying, B flies up)

Detail "Auto" lowers the resolution when the picture can't keep up.


IF SOMETHING GOES WRONG
-----------------------
* If Clove can't start, the GarlicOS menu appears instead. Clove writes
  CFW/clove/clove.log and CFW/clove/crash.log; those two files explain
  what happened.
* Buttons not responding: Settings > Controls > Input test, then put the
  codes it shows into CFW/clove/input.json.
* To use the GarlicOS menu again: Settings > About Clove > Switch to the
  GarlicOS menu. To come back, run Apps > "Switch to Clove" in GarlicOS.


REMOVING CLOVE
--------------
On your computer: delete CFW/main, rename CFW/main.garlic back to
CFW/main, then delete CFW/clove and CFW/autostart/clove-install.sh.
Optionally delete CFW/retroarch/.retroarch/cores/blockbox_libretro.so,
Roms/BLOCKBOX and Roms/APPS/Switch to Clove.sh.


CREDITS
-------
GarlicOS by Black-Seraph. Clove is a separate launcher that runs on top of
it and contains none of GarlicOS's code.
stb_truetype / stb_image / stb_vorbis (Sean Barrett, public domain),
dr_mp3 / dr_flac / dr_wav (David Reid, public domain), cJSON (MIT),
DejaVu Sans Mono glyphs in Blockbox (Bitstream Vera licence).
Clove and Blockbox: MIT licence (see LICENSE.txt).
