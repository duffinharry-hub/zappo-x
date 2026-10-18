#!/bin/sh
# The Zappo X test pack: things to try Zappo X with on the handheld, all made
# here (no one else's music or films, so it can go anywhere):
#   music/   three short songs with Zappo covers (tools/testsongs.py)
#   videos/  "Zappo Snowfall" (20 s of the Ice background, gentle chords) and
#            "Sound and Picture Check" (15 s: a beep and a flash each second,
#            to see that sound and picture keep together)
#   usage: tools/make-test-pack.sh OUT_DIR
set -e
cd "$(dirname "$0")/.."
OUT=${1:?OUT_DIR}
mkdir -p "$OUT/music" "$OUT/videos"
python3 tools/testsongs.py "$OUT/music" package/CFW/clove/zappos
make -s build/scene_preview
T=$(mktemp -d)
# Snowfall: the Ice scene over the Zappo meadow, with a slow chord pad
./build/scene_preview art/zappo_friends.png 4 20 30 > "$T/snow.ppm"
ffmpeg -v error -y -f lavfi -i "sine=f=220:d=20,volume=0.12" -f lavfi -i "sine=f=277.18:d=20,volume=0.10" \
	-f lavfi -i "sine=f=329.63:d=20,volume=0.10" -filter_complex "[0][1][2]amix=inputs=3,tremolo=f=0.25:d=0.5,afade=t=in:d=2,afade=t=out:st=17:d=3[a]" \
	-map "[a]" "$T/pad.wav"
ffmpeg -v error -y -f image2pipe -framerate 30 -c:v ppm -i "$T/snow.ppm" -i "$T/pad.wav" \
	-c:v libx264 -preset slow -crf 24 -pix_fmt yuv420p -c:a aac -b:a 96k -shortest \
	-metadata title="Zappo Snowfall" "$OUT/videos/Zappo Snowfall.mp4"
# Sound and Picture Check: test picture and counter, white flash + beep on each second
ffmpeg -v error -y -f lavfi -i "testsrc2=size=640x480:rate=30:duration=15" \
	-f lavfi -i "sine=f=1000:d=15,volume=0.4" \
	-filter_complex "[0]drawbox=enable='lt(mod(t\,1)\,0.1)':color=white:t=fill[v];[1]volume=enable='gte(mod(t\,1)\,0.1)':volume=0[a]" \
	-map "[v]" -map "[a]" -c:v libx264 -preset slow -crf 26 -pix_fmt yuv420p -c:a aac -b:a 96k \
	-metadata title="Sound and Picture Check" "$OUT/videos/Sound and Picture Check.mp4"
rm -rf "$T"
cat > "$OUT/READ ME.txt" <<'TXT'
Zappo X test pack - made for trying Zappo X out; everything in it was made by
the Zappo project and can be shared freely.

music/   three short songs (Electric Sparks, Crystal Shards, Ember Run) with
         Zappo covers: try the Music tab, Now Playing, the sleep timer.
videos/  Zappo Snowfall - 20 seconds of the Ice background with soft chords.
         Sound and Picture Check - a flash and a beep every second: they
         should come together.

The kit puts these in Music\Zappo Test Pack and Videos\Zappo Test Pack.
Knulli itself comes with a few free games to try (Ports and some consoles).
TXT
du -sh "$OUT"/*
