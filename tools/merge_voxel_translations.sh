#!/bin/sh
set -e
cd "$(dirname "$0")/.."
for po in lang/voxel/*.po; do
    lang=$(basename "$po" .po)
    mo="lang/mo/$lang/LC_MESSAGES/cataclysm-dda.mo"
    [ -f "$mo" ] || continue
    tmp=$(mktemp -d)
    if [ -f "$mo.base" ]; then
        cp "$mo.base" "$tmp/base.mo"
    else
        cp "$mo" "$mo.base"
        cp "$mo" "$tmp/base.mo"
    fi
    msgunfmt "$tmp/base.mo" -o "$tmp/base.po"
    msgcat --use-first "$po" "$tmp/base.po" -o "$tmp/merged.po"
    msgfmt "$tmp/merged.po" -o "$mo"
    rm -rf "$tmp"
    echo "merged $po into $mo"
done
