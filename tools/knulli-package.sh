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
cp build/zappo-x knulli/zappo-x.sh knulli/mpv-input.conf $P/zappo-x/
cp third_party/fonts/*.ttf third_party/fonts/LICENSES-OFL.txt $P/zappo-x/font/
for f in package/CFW/clove/*; do
	case "$(basename "$f")" in clove|main.sh) continue ;; esac
	cp -a "$f" $P/zappo-x/
done
tar -C $P -cf build/zappo-x-knulli.tar .
du -sh $P build/zappo-x-knulli.tar
