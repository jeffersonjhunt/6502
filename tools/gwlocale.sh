#!/bin/sh
# GW-BASIC's images the same under any locale (hw/testing.md, the gate's steps): oodos/gwbasic built twice, under
# LC_ALL=C and under en_US.UTF-8, each into a directory of its own, and GWBASIC.COM and GWBASIC.OVL compared byte
# for byte. $(wildcard) lists in the locale's collation and the link's order is the images' layout, so a source
# list left unsorted (the Makefile's $(sort ...)) shows here. Skipped, with a message, where en_US.UTF-8 isn't
# installed. `tools/check.sh changed` runs it when oodos/gwbasic/ or oodos/lib/ changes (tools/test-map.txt,
# gwlocale); quick and full always.
repo=$(cd "$(dirname "$0")/.." && pwd)
if ! locale -a 2>/dev/null | grep -qiE '^en_US\.utf-?8$'; then
    echo "gwlocale: en_US.UTF-8 isn't installed here, so there is nothing to compare: skipped"
    exit 0
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/gwlocale.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT
for loc in C en_US.UTF-8; do
    if ! LC_ALL=$loc make -C "$repo/oodos/gwbasic" BUILD="$tmp/$loc" "$tmp/$loc/GWBASIC.COM" >"$tmp/$loc.log" 2>&1; then
        cat "$tmp/$loc.log"
        echo "gwlocale: the build under $loc failed"
        exit 1
    fi
done
rc=0
for f in GWBASIC.COM GWBASIC.OVL; do
    if cmp -s "$tmp/C/$f" "$tmp/en_US.UTF-8/$f"; then
        echo "gwlocale: $f the same under LC_ALL=C and en_US.UTF-8"
    else
        echo "gwlocale: $f DIFFERS between LC_ALL=C and en_US.UTF-8 (a source list in the locale's order?)"
        rc=1
    fi
done
exit $rc
