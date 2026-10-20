#!/bin/sh
set -e
cd "$(dirname "$0")/.."
TAG=${1:-$(gh release list -R CleverRaven/Cataclysm-DDA -L 1 --json tagName -q '.[0].tagName')}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
echo "fetching assets from $TAG"
gh release download "$TAG" -R CleverRaven/Cataclysm-DDA -D "$WORK" \
    -p "cdda-linux-with-graphics-and-sounds-x64-*.tar.gz"
mkdir -p "$WORK/x"
tar -xzf "$WORK"/cdda-linux-with-graphics-and-sounds-x64-*.tar.gz -C "$WORK/x"
ROOT=$(ls -d "$WORK"/x/*/)
for d in "$ROOT"gfx/*/; do
    n=$(basename "$d")
    [ -e "gfx/$n" ] || cp -r "$d" gfx/
done
mkdir -p data/sound lang/mo
for d in "$ROOT"data/sound/*/; do
    n=$(basename "$d")
    [ -e "data/sound/$n" ] || cp -r "$d" data/sound/
done
cp -rn "$ROOT"lang/mo/* lang/mo/
sh tools/merge_voxel_translations.sh
echo "done"
