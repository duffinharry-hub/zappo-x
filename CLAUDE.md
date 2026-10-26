# Zappo / Zappo X - notes for Claude Code

Launcher (menu) for retro handhelds, one C99 code base:

- **Zappo** - original Anbernic RG35XX on GarlicOS (ARMv7, framebuffer). Released 0.3.6.
- **Zappo X** - Anbernic RG40XX H (Allwinner H700, Mali-G31, 640x480, 1 GB) on
  **Knulli** Gladiator II (`-DZAPPO_KNULLI`, SDL2 + GLES on the Knulli sysroot).
  Takes Knulli's menu's place and starts games through Knulli's `emulatorlauncher`.
- Also: a browser build (`make web`, WebAssembly), a tablet build, BLOCKBOX (a libretro core).

The owner is CovGNG. Standards: crisp, nothing sloppy; never delete user data (move it
aside); ask before anything irreversible; the stock Anbernic card is never written.

## Layout

| Path | What |
|---|---|
| `src/` | the launcher. `ui.c` (screens, menus), `data.c` + `data_knulli.inc` (consoles, scanning), `import.c` (sorting dropped files), `net.c` (Wi-Fi, Bluetooth, jobs), `plat_knulli.c` / `plat_evdev.inc` (Knulli platform, input by button code), `scene.c` (moving backgrounds) |
| `knulli/` | what goes on the handheld: `zappo-x.sh` (start loop), `zappox` (service), `install.sh`, `send-to-zappo.py` (the phone/PC upload page: files, updates, port zips), `zappo-update-check.py` (GitHub updater), `zappo-report.sh`, `postshare.sh` (card hook) |
| `tools/` | `knulli-package.sh` (builds `build/zappo-x-knulli.tar`), `knulli-kit.sh` (the PC kit), `publish.sh` |
| `twin/` | "the twin": QEMU model of the RG40XX H running real Knulli, driven by `twin/twinctl.py` (screens, buttons, shell). Linux only |
| `package/`, `pc/` | Zappo (RG35XX) release kit and Windows tools |

## Building

- `make host` - desktop test build (`build/clove-host`), Linux, needs zlib.
- `make knulli` - needs clang with an aarch64 Knulli sysroot (`KNULLI_SYSROOT`, built by
  `twin/gpu/mksysroot.py`). Then `sh tools/knulli-package.sh` -> `build/zappo-x-knulli.tar`.
- On Windows use WSL (Ubuntu) for these; the code is plain C, no Windows build.
- Version string: `CLOVE_VERSION` in `src/util.h` ("X 0.1.6"). Bump it for each release.

## Shipping an update to the handheld

1. Build and package as above.
2. Either drop the tar on the handheld's Send to Zappo page (Update), or publish a GitHub
   release tagged `ZappoX-<version>` with `zappo-x-knulli.tar` attached: the handheld's
   About > Check for updates finds it (checks GitHub's SHA-256).
3. On the handheld: Settings > About > Install the update.

## Knulli facts that matter

- Card: BATOCERA (vfat, `/boot`) + SHARE (exFAT, `/userdata`). Games `/userdata/roms/<system>`,
  settings `/userdata/system/batocera.conf`, Zappo X in `/userdata/system/zappo-x`.
- Buttons: RG40XX-H codes 304-317 from `es_input.cfg`; match by code, never by SDL index.
- Lights: `knulli-rgb-led-daemon`, keys `led.*` in batocera.conf.
- Wi-Fi: connman. The `wireless_hybrid_fix` service breaks it (no address) - Zappo X offers to switch it off.
- PSP plays `.iso .cso .pbp .chd` only (zips are unpacked by the sorter); DS takes `.nds .zip .7z`.
- A stock Anbernic card in TF2 is listed read-only (`STOCK_DIRS` in `data.c`).

## Working rules

- Every change: build `knulli` and `host` cleanly; when the twin is available, rehearse there.
- Keep messages on screen short and plain English ("Couldn't join X: check the password").
- Commit messages say what changed for the player, then the technical detail.
