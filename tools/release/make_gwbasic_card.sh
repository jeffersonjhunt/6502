#!/bin/sh
# One-off GW-BASIC test cards for the catalog (owner, 2026-11-18): a demo card
# (raw image, .img or .img.gz) plus GWBASIC.COM/OVL and the repo's basic/*.bas on
# A: (text: LF -> CR LF, ^Z; non-ASCII punctuation made ASCII), README.TXT's
# GW-BASIC section before its "START, on the front panel" paragraph.
# usage: make_gwbasic_card.sh DEMO.img[.gz] GWDIR BASICDIR README_SECTION LABEL OUT.img.gz
set -eu
tmpout="$6.part"
demo=$1 gw=$2 bas=$3 sec=$4 label=$5 out=$6
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
case "$demo" in *.gz) gunzip -c "$demo" > "$work/card.img";; *) cp "$demo" "$work/card.img";; esac
cp "$gw/GWBASIC.COM" "$gw/GWBASIC.OVL" "$work/"; cp "$sec" "$work/gwbasic-readme-section.txt"; mkdir "$work/bas"
# SKIP: names left off (upper case, space-separated); by default LIFEGFX, which needs
# GW-BASIC's graphics (phase 7e). Set SKIP= to put every program on.
skip=${SKIP-LIFEGFX}
for f in "$bas"/*.bas; do
  n=$(basename "$f" .bas | tr a-z A-Z)
  case " $skip " in *" $n "*) echo "left off: $n" >&2; continue;; esac
  # CP/M names are 8.3 (cpmcp drops the extension of a longer name): cut to 8, as DOS
  # would (FRACTALGFX -> FRACTALG), and refuse two programs that cut the same.
  c=$(printf %s "$n" | cut -c1-8)
  if [ "$c" != "$n" ]; then echo "named $c: $n" >&2; n=$c; fi
  if [ -e "$work/bas/$n.BAS" ]; then echo "two programs named $n" >&2; exit 1; fi
  python3 -I -c "import sys; t=open(sys.argv[1],encoding='utf-8').read(); t=t.replace('’',\"'\").replace('‘',\"'\").replace('“','\"').replace('”','\"').replace('…','...'); t.encode('ascii'); open(sys.argv[2],'w',newline='\n').write(t)" "$f" "$work/bas/$n.BAS"
done
cp "$(dirname "$0")/../../oodos/diskdefs" "$work/diskdefs"
tar -C "$work" -cf - . | docker run --rm -i -e LABEL="$label" debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61 sh -euc '
mkdir /w; cd /w; tar xf -; apt-get -qq update >/dev/null 2>&1; apt-get -qq install -y mtools cpmtools python3 >/dev/null 2>&1
cat diskdefs >> /etc/cpmtools/diskdefs
I=card.img@@1048576
mcopy -n -i $I ::/OODOS/A.IMG A.IMG
cpmls -f oodos-8m A.IMG | grep -qi gwbasic.com || cpmcp -f oodos-8m A.IMG GWBASIC.COM GWBASIC.OVL 0:
cpmcp -t -f oodos-8m A.IMG 0:readme.txt OLD.TXT
python3 - "$LABEL" <<PY
import sys
old = open("OLD.TXT", "rb").read().split(b"\x1a")[0].decode("ascii").replace("\r\n", "\n")
sec = open("gwbasic-readme-section.txt").read()
mark = "START, on the front panel"
assert mark in old
new = old.replace(mark, sec + mark, 1)
open("README.TXT", "w", newline="\n").write(new)
PY
cpmrm -f oodos-8m A.IMG 0:readme.txt
cpmcp -t -f oodos-8m A.IMG README.TXT 0:
for f in bas/*.BAS; do cpmrm -f oodos-8m A.IMG "0:$(basename $f | tr A-Z a-z)" 2>/dev/null || true; cpmcp -t -f oodos-8m A.IMG "$f" 0:; done
fsck.cpm -f oodos-8m -n A.IMG >&2
cpmls -f oodos-8m A.IMG >&2
mcopy -o -i $I A.IMG ::/OODOS/A.IMG
gzip -9 -c card.img' > "$tmpout"
mv "$tmpout" "$out"
ls -l "$out"
