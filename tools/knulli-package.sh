#!/bin/sh
# Lays out Zappo X for Knulli in build/knulli-pkg (and build/zappo-x-knulli.tar):
#   install.sh, uninstall.sh, zappox (the start-up service)
#   zappo-x/  -> /userdata/system/zappo-x: the program, its start loop,
#             menu fonts, clock fonts, themes, icons, lock screens, wallpaper
# Build the program first: make knulli
set -e
cd "$(dirname "$0")/.."
[ -x build/zappo-x ] || { echo "build/zappo-x missing: make knulli" >&2; exit 1; }
P=build/knulli-pkg
rm -rf $P && mkdir -p $P/zappo-x/font
cp knulli/install.sh knulli/uninstall.sh knulli/zappox "knulli/Zappo X.sh" $P/
cp build/zappo-x knulli/zappo-x.sh knulli/mpv-input.conf knulli/send-to-zappo.py knulli/get-box-art.py knulli/zappo-report.sh knulli/zappo-update-check.py $P/zappo-x/
ver="$(sed -n 's/.*CLOVE_VERSION "\(.*\)".*/\1/p' src/util.h | head -n1)"
printf '%s (%s, %s)\n' "$ver" "$(git rev-parse --short HEAD 2>/dev/null || echo local)" "$(date +%Y-%m-%d)" > $P/zappo-x/VERSION
cp third_party/fonts/*.ttf third_party/fonts/LICENSES-OFL.txt $P/zappo-x/font/
for f in package/CFW/clove/*; do
	case "$(basename "$f")" in clove|main.sh) continue ;; esac
	cp -a "$f" $P/zappo-x/
done
# a picture for each console (RetroArch's Systematic icons: tools/knulli_icons.py)
mkdir -p $P/zappo-x/icons && cp knulli/icons/*.png $P/zappo-x/icons/
tar -C $P -cf build/zappo-x-knulli.tar .
du -sh $P build/zappo-x-knulli.tar
