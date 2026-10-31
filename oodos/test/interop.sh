#!/bin/sh
# Checks an OODOS card with cpmtools: /OODOS/A.IMG taken off the card's FAT
# by mtools, then the stamps' order (sfcb.py), fsck.cpm, the label's mode,
# cpmls -F's stamps, and every file's contents by cpmcp. Runs in a throwaway
# container:
#
#   docker run --rm -v "$(hostpath oodos):/o:ro" -v "$(hostpath DIR):/c:ro" \
#       debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61 \
#       sh /o/test/interop.sh /c/CARD [--written | --ccp | --utils]
#
# CARD is a raw card image or a sparse one (oo1-sparse, as
# emulator/test/host/sd/oodos.sparse, the card make_card.sh made). With
# --written it is the card test_oodos_card saved (OODOS_CARD_OUT), and
# 0:OO1.BIN, the file the OO-1 wrote, is checked too: cpmls -F's stamps and
# its SFCB slots' bytes: the stamps in its first entry's, 0 in its second's
# (CP/M 3 keeps a file's stamps in its extent-zero entry's slot only, and
# OODOS's BDOS leaves the others 0). With --ccp it is the card
# test_oodos_card saved after the CCP's ERA and REN (OODOS_CCP_OUT, phase
# 4a): 0:README.TXT renamed README.DOC and 0:PATTERN.BIN (two entries)
# PATTERN.DAT, their stamps and contents kept; 3:SMALL.BIN and 0:HELLO.PRL
# erased, gone from the listing; the rest as make_card.sh made it. With
# --utils it is the card test_oodos_card saved after phase 4b1's utilities
# (OODOS_UTILS_OUT): ERASE's [CONFIRM] erased 0:HELLO.PRL, RENAME's wildcard
# made PATTERN.BIN PATTERN.DAT, SET made README.TXT RO, archived and F1, and
# WRITEOO1.COM SYS; INITDIR took the SFCBs away, SET turned password
# protection on and gave PATTERN.DAT (Write) and TESTSYS.SYS (Delete)
# passwords, so XFCBs in the free SFCB places, then INITDIR made the SFCBs
# again, the XFCBs moved out first (to 13, HELLO.PRL's, and 29), every stamp
# 0; SET asked the label for creation and update stamps again and named it
# OODOS.4B1 (its mode 0xB1).
# cpmtools reads ./diskdefs, so it runs in /o. Exits
# non-zero on the first failure; "PASS n" at the end counts the checks.
#
# fsck.cpm 2.23 compares the stamps in an SFCB's first slot backwards: it
# warns "Modification date earlier than creation date (extent=S/E)" for an
# entry E (E & 3 = 0, its SFCB S = E | 3) updated on a later day than it was
# made, and misses one updated before. OO1.BIN, made a day before its update,
# had its first entry there until phase 4b's utilities came before it (now
# entries 29 and 30, an SFCB's second and third slots). So sfcb.py checks
# every file entry's stamp order itself first, and fsck_clean lets through only that warning, only for
# first-slot entries sfcb.py found a day or more apart and in order.
set -eu
card=$1
written=${2:-}
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cpmtools mtools python3 >/dev/null 2>&1
dpkg-query -W -f '${Package} ${Version}\n' cpmtools mtools python3 | sed 's/^/     /'
cd /o
. "$(dirname "$0")/cpmtools.sh"
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

entries=$(def oodos-8m maxdir)
order=$(python3 -I test/sfcb.py "$img" "$entries" check "$work/allowed") || { echo "$order"; fail "sfcb.py: stamps out of order"; }
echo "$order" | sed 's/^/     /'
ok "sfcb.py: every file's first entry has an SFCB slot, each stamped one updated no earlier than made"

fsck_clean oodos-8m "$img" "$work/allowed"
ok "fsck.cpm is clean ($(grep -c . "$work/allowed" || true) first-slot warnings checked by sfcb.py)"

if [ "$written" = --utils ]; then
  [ "$(od -An -tu1 -j12 -N1 "$img" | tr -d ' ')" = 177 ] || fail "the label's mode is not 0xB1"
  [ "$(dd if="$img" bs=1 skip=1 count=11 status=none)" = 'OODOS   4B1' ] || fail "the label is not OODOS.4B1"
  ok "the label is OODOS.4B1, its mode 0xB1: passwords, creation and update stamps"
else
  [ "$(od -An -tu1 -j12 -N1 "$img" | tr -d ' ')" = 49 ] || fail "the label's mode is not 0x31"
  ok "the label's mode is 0x31: creation and update stamps"
fi

listing=$(cpmls -f oodos-8m -F "$img") || fail "cpmls -F"
echo "$listing" | sed 's/^/     /'
# A file's line in cpmls -F: the name, then its update and creation stamps.
stamped() {
  echo "$listing" | grep -E "^$1 +$2 .* $3  $4 *\$" >/dev/null || fail "$1.$2: not stamped update $3, created $4"
}
if [ "$written" != --ccp ] && [ "$written" != --utils ]; then
  for f in 'README TXT' 'PATTERN BIN' 'USER3 TXT' 'SMALL BIN'; do
    stamped $f '10/26/26 12:00' '10/26/26 12:00'
  done
  ok "cpmls -F: the card's four files, made and updated 10/26/26 12:00"

  for f in 0:README.TXT 0:PATTERN.BIN 3:USER3.TXT 3:SMALL.BIN; do
    cpmcp -f oodos-8m "$img" "$f" "$work/out" || fail "cpmcp $f"
    cmp -s "$work/${f#?:}" "$work/out" || fail "$f is not what make_card.sh put there"
  done
  ok "the card's files, out by cpmcp, unchanged"
fi

if [ "$written" = --written ]; then
  stamped OO1 BIN '10/26/26 15:30' '10/25/26 13:45'
  ok "cpmls -F: 0:OO1.BIN, made 10/25/26 13:45, updated 10/26/26 15:30"
  want="$(python3 -I test/sfcb.py stamp 2026-10-25 13:45)$(python3 -I test/sfcb.py stamp 2026-10-26 15:30)"
  slots=$(python3 -I test/sfcb.py "$img" "$entries" slots 0 OO1.BIN)
  echo "$slots" | sed 's/^/     /'
  [ "$(echo "$slots" | wc -l)" = 2 ] || fail "0:OO1.BIN has not two entries"
  [ "$(echo "$slots" | sed -n 1p)" = "29 $want" ] || fail "0:OO1.BIN's first entry (29)'s SFCB slot isn't $want"
  [ "$(echo "$slots" | sed -n 2p)" = "30 0000000000000000" ] || fail "0:OO1.BIN's second entry (30)'s SFCB slot isn't 0"
  ok "0:OO1.BIN's first entry's SFCB slot holds $want, its second's 0"
  cpmcp -f oodos-8m "$img" 0:OO1.BIN "$work/out" || fail "cpmcp 0:OO1.BIN"
  cmp -s "$work/OO1.BIN" "$work/out" || fail "0:OO1.BIN is not what the OO-1 wrote"
  ok "0:OO1.BIN, out by cpmcp, is what the OO-1 wrote"
elif [ "$written" = --ccp ]; then
  for f in 'README DOC' 'PATTERN DAT' 'USER3 TXT'; do
    stamped $f '10/26/26 12:00' '10/26/26 12:00'
  done
  ok "cpmls -F: README.DOC and PATTERN.DAT, renamed, keep their stamps; USER3.TXT is as it was"
  for f in 'README +TXT' 'PATTERN +BIN' 'SMALL +BIN' 'HELLO +PRL'; do
    if echo "$listing" | grep -E "^$f " >/dev/null; then fail "cpmls -F still lists $f"; fi
  done
  echo "$listing" | grep -E '^HELLO +COM ' >/dev/null || fail "cpmls -F: HELLO.COM is gone"
  ok "cpmls -F: README.TXT, PATTERN.BIN, 3:SMALL.BIN and HELLO.PRL gone, HELLO.COM kept"
  for f in 0:README.DOC:README.TXT 0:PATTERN.DAT:PATTERN.BIN 3:USER3.TXT:USER3.TXT; do
    name=${f%:*}
    cpmcp -f oodos-8m "$img" "$name" "$work/out" || fail "cpmcp $name"
    cmp -s "$work/${f##*:}" "$work/out" || fail "$name is not ${f##*:} as make_card.sh made it"
  done
  ok "README.DOC, PATTERN.DAT and USER3.TXT, out by cpmcp, are what make_card.sh made"
elif [ "$written" = --utils ]; then
  # A line of cpmls -F: the name, kilobytes, records, the attributes (F1-F4 as 1-4, R for RO, S
  # for SYS, A for archived, in that order of columns), the protection, then the two stamps,
  # 0 (cpmls shows day 0 as 12/31/77 00:00) since INITDIR made the SFCBs again.
  line() {
    echo "$listing" | grep -E "^$1 +$2 +[0-9]+k +[0-9]+ $3 +$4 +12/31/77 00:00  12/31/77 00:00 *\$" >/dev/null ||
      fail "$1.$2: not listed with attributes '$3', protection $4 and stamps 0"
  }
  line README TXT '1   R A' None
  line PATTERN DAT '   ' Write
  line TESTSYS SYS '   ' Delete
  line WRITEOO1 COM ' +S' None
  for f in 'OODOS SYS' 'USER3 TXT' 'SMALL BIN' 'HELLO COM' 'DIR COM' 'ERASE COM' 'RENAME COM' 'SET COM' 'INITDIR COM'; do
    line $f '.*' None
  done
  ok "cpmls -F: README.TXT 1 R A, WRITEOO1.COM S, PATTERN.DAT Write, TESTSYS.SYS Delete, the rest None; stamps 0"
  for f in 'PATTERN +BIN' 'HELLO +PRL'; do
    if echo "$listing" | grep -E "^$f " >/dev/null; then fail "cpmls -F still lists $f"; fi
  done
  ok "cpmls -F: HELLO.PRL erased, PATTERN.BIN renamed"
  # The directory itself: an SFCB in every fourth entry again, and the XFCBs (cpmls -F lists
  # them as user 16's) where INITDIR moved them, with their modes (bit 7 Read, 6 Write, 5 Delete).
  cat >"$work/xfcb.py" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read(int(sys.argv[2]) * 32)
e = [d[i * 32:(i + 1) * 32] for i in range(len(d) // 32)]
print('sfcbs', sum(1 for i in range(3, len(e), 4) if e[i][0] == 0x21),
      'other', sum(1 for i in range(len(e)) if e[i][0] == 0x21 and i & 3 != 3))
for i, x in enumerate(e):
    if 16 <= x[0] <= 31:
        print('xfcb', i, x[0] - 16, x[1:12].decode('ascii', 'replace'), '%02x' % (x[12] & 0xE0))
PY
  dir=$(python3 -I "$work/xfcb.py" "$img" "$entries")
  echo "$dir" | sed 's/^/     /'
  [ "$(echo "$dir" | sed -n 1p)" = "sfcbs $((entries / 4)) other 0" ] || fail "not an SFCB in every fourth entry, and only there"
  [ "$(echo "$dir" | sed -n '2,$p')" = "xfcb 13 0 PATTERN DAT 40
xfcb 29 0 TESTSYS SYS 20" ] || fail "the XFCBs are not PATTERN.DAT's (13, Write) and TESTSYS.SYS's (29, Delete)"
  ok "an SFCB every fourth entry; the XFCBs in 13 and 29, Write and Delete"
  for f in 0:README.TXT:README.TXT 0:PATTERN.DAT:PATTERN.BIN 3:USER3.TXT:USER3.TXT 3:SMALL.BIN:SMALL.BIN; do
    name=${f%:*}
    cpmcp -f oodos-8m "$img" "$name" "$work/out" || fail "cpmcp $name"
    cmp -s "$work/${f##*:}" "$work/out" || fail "$name is not ${f##*:} as make_card.sh made it"
  done
  ok "README.TXT, PATTERN.DAT, USER3.TXT and SMALL.BIN, out by cpmcp, are what make_card.sh made"
elif [ -n "$written" ]; then
  fail "$written: not an option"
fi

echo "PASS $passed"
