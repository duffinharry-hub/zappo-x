Zappo 0.3.6 for the **original Anbernic RG35XX (2022 model) on GarlicOS**.

**0.3.6:** stand-alone ports hooked into GarlicOS as launcher scripts (the DraStic DS port's `"NDS": "/mnt/mmc/Roms/APPS/Drastic.sh"` in coremapping.json, and ports like it) now start from Zappo the way GarlicOS starts them - the script with the game, at full clock. Apps in `Roms/APPS` (Dingux Commander, GMU and friends) were already listed under Apps and run as before. Nothing else changed; the Windows setup is the same.

**Download `Zappo-0.3.6.zip`**, unzip, put your GarlicOS card in a Windows PC and run **`Zappo Setup.bat`**. It finds the card, you tick what you want done (install / update, boot logo and charging animation, films and music from Downloads, box art, the extra cores pack, a read-back check), and it can stay on the PC like any program (Start menu, Apps & features). Everything it changes on the card can be undone from the same window, including taking Zappo off again. `Zappo.bat` is the same kit as a text menu; `zappo.sh` covers install and undo on Linux / macOS.

One card is enough: GarlicOS and Zappo run from the TF1 card; a second card in TF2 is optional, for more games.

No GarlicOS, RetroArch, emulator core or BIOS files are in the download. Back your card up first (an image with Win32 Disk Imager or balenaEtcher).

`Zappo manual.txt` in the zip is the full guide; `READ ME.txt` covers the kit. Source: this repository (MIT). Created by CovGNG.
