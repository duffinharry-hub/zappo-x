# Third-party code, fonts and data in Zappo

Zappo's own code and artwork are MIT (see LICENSE). These parts come from elsewhere:

## Code compiled into the launcher (`third_party/`)
| Library | Licence | Used for |
|---|---|---|
| cJSON (Dave Gamble) | MIT | settings, themes, system lists |
| stb_image, stb_image_write, stb_truetype (Sean Barrett) | public domain / MIT | pictures, screenshots, text |
| stb_vorbis (Sean Barrett) | public domain / MIT | OGG playback |
| dr_mp3, dr_flac, dr_wav (David Reid) | public domain / MIT-0 | MP3, FLAC, WAV playback |
| QR Code generator (Project Nayuki) | MIT | the Send to Zappo address as a QR code |

## Blockbox (`blockbox/vendor/libretro.h`)
`libretro.h`, the libretro API header - MIT, (C) 2010-2024 The RetroArch team. Blockbox itself is MIT.

## Fonts (`package/CFW/clove/fonts/`)
All under the SIL Open Font License 1.1; the full licence text and each copyright notice are in `LICENSES-OFL.txt` beside them: Outfit, Big Shoulders, Jura, Pixelify Sans, JetBrains Mono, Lora, Tektur, Poiret One (Denis Masharov).

The wordmark in the generated artwork uses Poppins (OFL, Indian Type Foundry) at build time; the font itself is not shipped.

## Not included - fetched or patched on the user's own PC
- **ffmpeg** (GPL/LGPL, gyan.dev builds): the Windows kit downloads it on first use into `tools\ffmpeg` with its licence; it is never redistributed by Zappo.
- **GarlicOS's FFmpeg RetroArch core**: Zappo does not ship it. `pc/tools/player-fix.ps1` changes 47 bytes of the user's own copy (an `lseek64` argument-order fix) after checking its MD5, and can restore it.
- **GarlicOS's `ramdisk.img`**: not shipped. `pc/tools/boot-art.ps1` rewrites the user's own copy with Zappo's charging pictures in place of the eight stock PNGs; everything else in it is unchanged. `art/ramdisk.py` is the same tool for Linux.
- **The "extra and updated rg35xx cores" pack** (community builds of libretro cores, mostly GPL): optional, downloaded by the user, installed by `pc/tools/extra-cores.ps1`. Not part of Zappo.
- **Box art and posters** come from thumbnails.libretro.com, TheGamesDB, Wikipedia, Deezer and iTunes at the user's request and stay on their card.

## The device twin (`twin/`, a development tool; nothing of it ships on a handheld)
- `twin/kernel/` (the AXP2202 stand-in, the virtio-gpu patch) and `twin/qemu/` (the virtio-sound patch): GPL-2.0, as the Linux kernel and QEMU they change.
- `twin/bt/zappo-twin-bt.c`: GPL-2.0-or-later; it is built with BlueZ's controller emulator (BlueZ, GPL-2.0-or-later / LGPL-2.1-or-later), which is downloaded at build time and not in this repository.
- Knulli, Mesa, SDL and QEMU themselves are downloaded and built by the twin's scripts, not kept here.

GarlicOS (Black-Seraph), Knulli, RetroArch, the emulator cores and any BIOS files are not part of this repository or its releases.
