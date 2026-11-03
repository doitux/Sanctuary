#!/usr/bin/env bash
# Sanctuary – render one listening demo per factory preset (~22 s: drone, then chords) and a
# combined Sanctuary-Demo.mp3.  Usage: bash tools/demo/make-demos.sh [build/sanctuary-render] [demo]
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
R=${1:-$ROOT/build/sanctuary-render}
OUT=${2:-$ROOT/demo}
mkdir -p "$OUT"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
N=$("$R" --list-presets | wc -l)
list=()
for ((i = 0; i < N; i++)); do
  name=$("$R" --list-presets | sed -n "$((i + 1))p" | sed -E 's/^[0-9]+ +//; s/  +.*//')
  file=$(echo "$name" | tr -cd '[:alnum:] &' | sed 's/ & /-/; s/ /-/g')
  "$R" "$ROOT/tools/demo/demo-drone.txt" "$TMP/d.wav" --mode drone --preset "$i" --tail 8 >/dev/null
  "$R" "$ROOT/tools/demo/demo-chords.txt" "$TMP/c.wav" --mode play --preset "$i" --tail 7 >/dev/null
  # chords enter 7 s in, while the drone fades; 0.5 s fade-out at the very end
  ffmpeg -v error -y -i "$TMP/d.wav" -i "$TMP/c.wav" -filter_complex \
    "[1:a]adelay=7000|7000[c];[0:a][c]amix=inputs=2:normalize=0:duration=longest,atrim=0:23,afade=t=out:st=22.3:d=0.7[o]" \
    -map "[o]" -ar 48000 "$TMP/$i.wav"
  ffmpeg -v error -y -i "$TMP/$i.wav" -codec:a libmp3lame -q:a 2 \
    -metadata title="Sanctuary – $name" -metadata artist="Sanctuary v0.2.0" "$OUT/Sanctuary-$((i + 1))-$file.mp3"
  ffmpeg -v error -y -f lavfi -i anullsrc=r=48000:cl=stereo -t 1 -c:a pcm_f32le "$TMP/gap.wav"
  list+=("$TMP/$i.wav" "$TMP/gap.wav")
  echo "  $OUT/Sanctuary-$((i + 1))-$file.mp3"
done
printf "file '%s'\n" "${list[@]}" > "$TMP/list.txt"
ffmpeg -v error -y -f concat -safe 0 -i "$TMP/list.txt" -c:a pcm_f32le "$TMP/all.wav"
ffmpeg -v error -y -i "$TMP/all.wav" -codec:a libmp3lame -q:a 2 -metadata title="Sanctuary – all presets" \
  -metadata artist="Sanctuary v0.2.0" "$OUT/Sanctuary-Demo.mp3"
echo "  $OUT/Sanctuary-Demo.mp3"
