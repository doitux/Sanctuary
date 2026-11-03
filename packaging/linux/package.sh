#!/usr/bin/env bash
# Sanctuary – build the Linux release zip from a finished build:
#   bash packaging/linux/package.sh [build-dir]   ->  dist/Sanctuary-<version>-linux-x86_64.zip
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${1:-$ROOT/build}
A=$B/Sanctuary_artefacts/Release
VER=$(sed -nE 's/^project\(Sanctuary VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")
D=$ROOT/dist/stage/Sanctuary
rm -rf "$ROOT/dist/stage"; mkdir -p "$D"
cp -R "$A/VST3/Sanctuary.vst3" "$A/CLAP/Sanctuary.clap" "$A/LV2/Sanctuary.lv2" "$A/Standalone/Sanctuary" "$D/"
strip --strip-unneeded "$D/Sanctuary.vst3/Contents/x86_64-linux/Sanctuary.so" "$D/Sanctuary.clap" \
  "$D/Sanctuary.lv2/libSanctuary.so" "$D/Sanctuary"
cp "$ROOT/packaging/linux/install.sh" "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/CREDITS.md" "$D/"
chmod +x "$D/install.sh" "$D/Sanctuary"
Z=$ROOT/dist/Sanctuary-$VER-linux-x86_64.zip
rm -f "$Z"
(cd "$ROOT/dist/stage" && zip -qr9 "$Z" Sanctuary)
rm -rf "$ROOT/dist/stage"
echo "$Z ($(du -h "$Z" | cut -f1))"
