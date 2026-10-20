# Zappo

An open-source launcher for the **original Anbernic RG35XX (2022 model)** running **GarlicOS**, with a music player, a Videos tab, folder-drop imports and a Windows kit.

Zappo replaces GarlicOS's menu. GarlicOS, RetroArch and the emulators stay as they are underneath; Zappo changes the menu, how games are started, and how files get onto the card. If Zappo ever fails to start, the handheld falls back to GarlicOS's own menu.

**Download:** the latest `Zappo-x.y.z.zip` under [Releases](https://github.com/duffinharry-hub/zappo-x/releases). Unzip, put the GarlicOS card in a Windows PC, run **`Zappo Setup.bat`**: it finds the card, you tick what you want done, and it can stay on the PC like any program (Start menu, Apps & features). `Zappo.bat` is the same kit as a text menu. Linux or macOS: `./zappo.sh install` with the card mounted (it does the same job, python3 needed for the player fix and charging animation). `Zappo manual.txt` in the zip (the same as [`package/README.txt`](package/README.txt)) is the full guide.

## Zappo X - for the RG40XX H on Knulli (in progress)

The next Zappo, for the **Anbernic RG40XX H** (H700) and its family running **Knulli**, built from the same code (`ZAPPO_KNULLI`). It takes the place of Knulli's menu and starts games through Knulli's own launcher, so Knulli's emulators and settings stay as they are; Knulli's menu is one tab away, and taking Zappo X off gives the handheld back to it.

New on top of what's below: Wi-Fi and Bluetooth screens, **Send to Zappo** (drop files from a phone or PC onto a page the handheld serves), box art downloads (libretro thumbnails, TheGamesDB), RetroAchievements sign-in, the **stick lights** following the section you're in, TV out over HDMI, and a **moving background** for each section (sparks, crystals, embers, drops, snow, leaves) drawn by the GPU under the menu.

**Kit:** `Zappo X Kit.zip` - unzip, put the Knulli card in a PC, run `Zappo X.bat` (Windows) or `./zappo-x-kit.sh install` (Linux / macOS). The handheld installs it at its next start; `READ ME.txt` in the kit has the details. It has been developed and tested on an emulated RG40XX H ("the twin", `twin/`); checks on the real handheld are still to come.

## What it does (Zappo 0.3.5, original RG35XX)

**Games** - a fixed CPU speed per console while you play (1.0 GHz for Game Boy/NES up to 1.5 GHz for PlayStation/arcade; GarlicOS changes the clock mid-game, which stutters), per-console emulator / scaling / threaded video / audio latency / FPS settings, carry-on-where-you-left-off with "Start over", search across everything, consoles that need a BIOS say which file, GarlicOS favourites, box art and skins carry over.

**Import folder** - drop anything into `Import\` and it is sorted: games by file type, PS1 / PC Engine CD / Sega CD discs by reading the disc, zips by their contents, arcade sets by MAME name, BIOS files to `BIOS\`, songs to `Music\`, videos to the Videos tab. Anything unrecognised waits for you to pick a console.

**Music** - a built-in player (MP3, FLAC, OGG, WAV; keeps playing while you browse) with Artists, Genres, Songs, Playlists (.m3u, plus Recently added and Most played) and Folders; cover art; full-screen Now Playing with 10 s / 1 min skipping; Play all / Shuffle all on any artist, genre or playlist; sleep timer; per-song volume levelling; a song you leave for a game is waiting afterwards.

**Videos** - a Videos tab as a poster wall or list with running times and watched ticks; up next inside a series folder; "Continue *series*" to the next unwatched episode; a patched FFmpeg player (GarlicOS's closes when you skip): 10 s / 1 min skipping, A pauses, B exits cleanly; films never resume at power-on; MP4s that aren't web-optimised are fixed in place on first play.

**Everyday** - lock screen (L1+R1 unlocks; music controls and A to pause while locked); screensaver clock with 8 fonts; 19 themes (including a Zappo for each element - Electric, Fire, Leaf, Crystal, Ice and Water - with his picture behind every screen, and a lock screen picture each), Classic or Glass style, Carousel / Grid / List / List with art / Shelf layouts; battery time left, 15% and 5% warnings, time to full while charging, how long a full charge lasts at your usage; weekly backup of saves and states (newest three kept); a quiet note at the next start if the handheld restarted on its own mid-game, with the logs kept; screenshot viewer; adjustable button repeat; BLOCKBOX, a small Minecraft-style building game.

**Windows kit (`Zappo Setup.bat`, or `Zappo.bat` as a text menu)** - a setup window that finds the card and runs what you tick; install / update and remove; films and music straight from Downloads to the card, with HD / oversized / multi-track films shrunk to 640 px, English audio, by ffmpeg; zipped series unpacked into folders; box art (libretro library, TheGamesDB), film posters (Wikipedia, else a frame grab) and music covers (Deezer, iTunes); a read-back check; the video player fix as a 47-byte patch to your own copy of GarlicOS's core; boot logo and charging animation (your `ramdisk.img` rewritten with the frames swapped, nothing else touched); an optional updated cores pack; and an undo for every one of those.

**What it isn't** - it's not an operating system, and it doesn't do Wi-Fi, Bluetooth or RetroAchievements: the original RG35XX has no wireless hardware. It is for the original RG35XX only, not the H / Plus / 2024 models.

## Repository

| Path | What |
|---|---|
| `src/` | the launcher, C99, no dependencies beyond libc (software renderer, stb_truetype, evdev input, framebuffer) |
| `blockbox/` | the BLOCKBOX libretro core: freestanding C, no libc, soft-float ABI to match GarlicOS's RetroArch |
| `package/` | what goes on the card: `CFW/clove` (binary, fonts, themes, lock screens, `main.sh`), `CFW/autostart/clove-install.sh`, the BLOCKBOX core and worlds, the manual |
| `pc/` | the Windows kit (PowerShell): the setup window (`Zappo Setup.ps1`, `tools/setup-core.ps1`), install, media, art, cores, boot pictures, check, undo |
| `art/` | the artwork generators (wallpapers, themes, boot logo, charging frames, ramdisk rewrite) |
| `tests/` | host test harness and unit tests |
| `third_party/` | cJSON, stb, dr_libs (see THIRD-PARTY.md) |
| `release/` | `make-release.sh` builds the release zips |
| `knulli/` | Zappo X on Knulli: start loop, service, installer, Send to Zappo and box-art scripts, the kit's start-up hook |
| `pc-x/` | the Zappo X kit for the PC (Windows / Linux / macOS); `tools/knulli-kit.sh` builds it |
| `twin/` | the emulated RG40XX H used for testing (QEMU, Knulli, stand-ins for the gamepad, lights, battery, Wi-Fi, Bluetooth) |

## Building

See [BUILD.txt](BUILD.txt). In short: `make host` (desktop build with ASan/UBSan, driven by `tests/shoot.sh`), `make arm` (static ARMv7 binary; `ARM_TOOLCHAIN=` any arm-linux-gnueabihf GCC - the release uses crosstool-NG 4.9.3 from raspberrypi/tools), `make -C blockbox arm`, then `release/make-release.sh`. The ARM binary is reproducible: the same source gives the same bytes.

## How it fits GarlicOS

GarlicOS runs `CFW/main` at start. Zappo's installer (run by GarlicOS from `CFW/autostart` on the first boot after copying) keeps that file as `CFW/main.garlic` and puts its own `main.sh` in its place; `main.sh` runs Zappo and, if Zappo exits with an error, GarlicOS's menu. Zappo launches games exactly the way GarlicOS does (RetroArch with the mapped core, the same per-system config overrides), adding a fixed CPU clock and its own video-player controls. Nothing of GarlicOS is modified except the video player core, which the Windows kit patches on your own card and can restore.

## Licence

MIT - see [LICENSE](LICENSE). Third-party code and fonts: [THIRD-PARTY.md](THIRD-PARTY.md). GarlicOS, RetroArch, the emulator cores and BIOS files are not part of this repository or its releases.

Created by CovGNG.
