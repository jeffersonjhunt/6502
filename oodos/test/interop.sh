#!/bin/sh
# Checks an OODOS card with cpmtools: /OODOS/A.IMG taken off the card's FAT
# by mtools, then fsck.cpm, the label's mode, cpmls -F's stamps, and every
# file's contents by cpmcp. Runs in a throwaway container:
#
#   docker run --rm -v "$(hostpath oodos):/o:ro" -v "$(hostpath DIR):/c:ro" \
#       debian:bookworm sh /o/test/interop.sh /c/CARD [--written]
#
# CARD is a raw card image or a sparse one (oo1-sparse, as
# emulator/test/host/sd/oodos.sparse, the card make_card.sh made). With
# --written it is the card test_oodos_card saved (OODOS_CARD_OUT), and
# 0:OO1.BIN, the file the OO-1 wrote, is checked too. cpmtools reads
# ./diskdefs, so it runs in /o. Exits non-zero on the first failure; "PASS n"
# at the end counts the checks.
set -eu
card=$1
written=${2:-}
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cpmtools mtools python3 >/dev/null 2>&1
cd /o
work=$(mktemp -d)
passed=0
ok() { passed=$((passed + 1)); echo "ok   $*"; }
fail() { echo "FAIL $*"; exit 1; }
export MTOOLS_SKIP_CHECK=1

# The files, as make_card.sh and test_oodos_card.cpp make them: a binary's
# byte i is ((i >> 7) * 31 + i * 7 + seed) & 0xFF; a text file is CR LF
# lines, ^Z to the end of its last record.
cat >"$work/gen.py" <<'PY'
import sys
name, size, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
open(name, 'wb').write(bytes((((i >> 7) * 31 + i * 7 + seed) & 0xFF) for i in range(size)))
PY
cat >"$work/text.py" <<'PY'
import sys
name, lines = sys.argv[1], sys.argv[2:]
b = ''.join(l + '\r\n' for l in lines).encode('ascii')
open(name, 'wb').write(b + b'\x1a' * (128 - len(b) % 128))
PY
# A sparse image expanded (sd_images.h's format), a raw one as it is; then
# the first partition's start, from the MBR (0 for a card without one).
cat >"$work/card.py" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
if data.startswith(b'oo1-sparse 1\n'):
    lines = data.decode('ascii').split('\n')
    out = bytearray(int(lines[1].split()[1]) * 512)
    for l in lines[2:]:
        if l:
            n, h = l.split()
            out[int(n) * 512:(int(n) + 1) * 512] = bytes.fromhex(h)
    data = bytes(out)
open(dst, 'wb').write(data)
mbr = data[510:512] == b'\x55\xaa' and data[450] in (0x01, 0x04, 0x06, 0x0b, 0x0c, 0x0e)
print(int.from_bytes(data[454:458], 'little') * 512 if mbr else 0)
PY

python3 "$work/gen.py" "$work/PATTERN.BIN" 36864 1
python3 "$work/gen.py" "$work/SMALL.BIN" 384 3
python3 "$work/gen.py" "$work/OO1.BIN" 33280 5
python3 "$work/text.py" "$work/README.TXT" 'THIS IS AN OODOS DRIVE, MADE ON LINUX BY CPMTOOLS.' \
    'OODOS READS IT AS DRIVE A:, SD UNIT 1, /OODOS/A.IMG ON THE CARD.'
python3 "$work/text.py" "$work/USER3.TXT" 'A FILE IN USER 3.'

offset=$(python3 "$work/card.py" "$card" "$work/card.img") || fail "$card: not a card image"
img="$work/A.IMG"
mcopy -n -i "$work/card.img@@$offset" ::/OODOS/A.IMG "$img" || fail "no /OODOS/A.IMG on the card"
[ "$(stat -c %s "$img")" = 8388608 ] || fail "A.IMG is not 8 MB"
ok "A.IMG off the card's FAT (partition at byte $offset), 8 MB"

# fsck.cpm -n exits 0 after some errors (a bad stamp, for one), so its
# report must have no Error or Warning lines either.
out=$(fsck.cpm -f oodos-8m -n "$img" 2>&1) || { echo "$out"; fail "fsck.cpm"; }
echo "$out" | sed 's/^/     /'
! echo "$out" | grep -E '^(Error|Warning)' >/dev/null || fail "fsck.cpm reports the errors above"
ok "fsck.cpm is clean"

[ "$(od -An -tu1 -j12 -N1 "$img" | tr -d ' ')" = 49 ] || fail "the label's mode is not 0x31"
ok "the label's mode is 0x31: creation and update stamps"

listing=$(cpmls -f oodos-8m -F "$img") || fail "cpmls -F"
echo "$listing" | sed 's/^/     /'
# A file's line in cpmls -F: the name, then its update and creation stamps.
stamped() {
  echo "$listing" | grep -E "^$1 +$2 .* $3  $4 *\$" >/dev/null || fail "$1.$2: not stamped update $3, created $4"
}
for f in 'README TXT' 'PATTERN BIN' 'USER3 TXT' 'SMALL BIN'; do
  stamped $f '10/26/26 12:00' '10/26/26 12:00'
done
ok "cpmls -F: the card's four files, made and updated 10/26/26 12:00"

for f in 0:README.TXT 0:PATTERN.BIN 3:USER3.TXT 3:SMALL.BIN; do
  cpmcp -f oodos-8m "$img" "$f" "$work/out" || fail "cpmcp $f"
  cmp -s "$work/${f#?:}" "$work/out" || fail "$f is not what make_card.sh put there"
done
ok "the card's files, out by cpmcp, unchanged"

if [ "$written" = --written ]; then
  stamped OO1 BIN '10/26/26 15:30' '10/26/26 13:45'
  ok "cpmls -F: 0:OO1.BIN, made 10/26/26 13:45, updated 15:30"
  cpmcp -f oodos-8m "$img" 0:OO1.BIN "$work/out" || fail "cpmcp 0:OO1.BIN"
  cmp -s "$work/OO1.BIN" "$work/out" || fail "0:OO1.BIN is not what the OO-1 wrote"
  ok "0:OO1.BIN, out by cpmcp, is what the OO-1 wrote"
elif [ -n "$written" ]; then
  fail "$written: not an option"
fi

echo "PASS $passed"
