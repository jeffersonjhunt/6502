#!/bin/sh
# Builds the OODOS cards: the test card, a FAT card image as a PC would make one
# (mkfs.fat, mtools, sfdisk) holding an OODOS drive made by cpmtools, so the
# emulator's tests read what Linux writes (hw/oodos.md, "Testing"). Needs
# Docker; everything runs in a throwaway debian:bookworm, pinned by digest,
# under faketime, so the same sources make the same card; it prints the
# packages' versions it used. Writes emulator/test/host/sd/oodos.sparse
# (sd_images.h reads it), and the demo card beside it, demo.sparse:
#
#   oodos/tools/make_card.sh
#
# The card: 32 MB, an MBR, FAT16 in partition 1 (type 06) at sector 2048,
# 2 KB clusters. On it:
#
#   /BOOT.BIN          OOLDR (oodos/ldr/ooldr.s), built here
#   /OODOS/DRIVES.TXT  oodos/card/DRIVES.TXT: A: and B:
#   /OODOS/A.IMG       oodos-8m, label mode 0x31, stamped 2026-10-26 12:00:
#     0:README.TXT     text, CR LF, ^Z to the end of its last record
#     0:PATTERN.BIN    36,864 bytes, seed 1: two directory entries (32 KB each)
#     3:USER3.TXT      text, as README.TXT
#     3:SMALL.BIN      384 bytes, seed 3
#     0:OODOS.SYS      OODOS (oodos/sys/), built here
#     0:TESTSYS.SYS    OOLDR's test system (oodos/ldr/testsys.s), 268
#                      records, two entries: test_oodos_boot names it
#                      OODOS.SYS in its own copy of the card
#     0:HELLO.COM      oodos/examples/hello.s, for the CCP to run; SYS, so
#                      the CCP finds it from any user (phase 4a)
#     0:HELLO.PRL      the same as a .PRL (oodos/Makefile), for phase 5's
#                      loader
#     0:WRITEOO1.COM   oodos/sys/test/writeoo1.s: test_oodos_card runs it to
#                      write 0:OO1.BIN through the BDOS, in the first free
#                      entries
#     0:DATE.COM ...   the utilities (oodos/utils/, make utils), one .COM
#                      each utils/NAME.s, in its name's order (DATE to
#                      TYPE); SYS, as HELLO.COM (hw/oodos.md, "The
#                      utilities, phase 4b"). The tests work out where
#                      each file's entries are from the card, so a utility
#                      more changes no test
#   /OODOS/B.IMG       oodos-8m, label mode 0x31, empty
#
# The demo card (demo.sparse), the one the WebUI puts in its slot at each
# visit (its build makes public/core/demo-card.img.gz of it): the same
# layout, the FAT volume labelled OODOS DEMO, and only what a visitor runs,
# nothing of the tests'. Its stamps, FAT and CP/M, are the date of the last
# commit to what goes on it (the oodos/ files copied in below and
# backplane.inc, the committer's date in UTC), so a visitor sees when it was
# made and the same commit still makes the same card; the test card keeps its
# fixed moment, which the tests read:
#
#   /BOOT.BIN, /OODOS/DRIVES.TXT   as the test card's
#   /OODOS/A.IMG       oodos-8m, label mode 0x31: README.TXT (what to try,
#                      in our words), OODOS.SYS, HELLO.COM and the utilities
#                      (SYS, as on the test card)
#   /OODOS/B.IMG       oodos-8m, label mode 0x31, empty: B: to work on
#
# A binary file's byte i is ((i >> 7) * 31 + i * 7 + seed) & 0xFF, a
# different run in each record; test_oodos_card.cpp and interop.sh know the
# same, and the text files' words.
set -eu
# debian:bookworm as of 2026-10-20.
image=debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/emulator/test/host/sd"
# What the container copies in (below), and so what's on the cards: a commit
# elsewhere in oodos/ (its tests, its README) leaves the stamp as it was.
src="oodos/Makefile oodos/include oodos/sys oodos/ldr oodos/examples oodos/cfg oodos/tools
     oodos/utils oodos/card oodos/diskdefs backplane/bios/backplane.inc"
# shellcheck disable=SC2086 # src is a list of paths
stamp=$(cd "$root" && TZ=UTC git -c safe.directory='*' log -1 --format=%cd \
    --date=format-local:'%Y-%m-%d %H:%M:%S' -- $src)
if [ -z "$stamp" ]; then
    echo "make_card: no commit of $src to stamp the demo card with" >&2
    exit 1
fi
# shellcheck disable=SC2086
if [ -n "$(cd "$root" && git -c safe.directory='*' status --porcelain -- $src)" ]; then
    echo "make_card: the card's sources have uncommitted changes; the demo card is stamped $stamp, the last commit's" >&2
fi
echo "the demo card's stamps: $stamp"
docker run --rm -i -e DEMO_STAMP="$stamp" -v "$(hostpath "$root/oodos"):/o:ro" \
    -v "$(hostpath "$root/backplane/bios"):/b:ro" -v "$(hostpath "$out"):/out" "$image" sh -eu <<'INNER'
apt-get -qq update >/dev/null
apt-get -qq install -y dosfstools mtools fdisk cpmtools cc65 make python3 faketime >/dev/null
echo "debian $(cat /etc/debian_version); the packages:"
dpkg-query -W -f '  ${Package} ${Version}\n' dosfstools mtools fdisk cpmtools cc65 make python3 faketime
cd /tmp
export MTOOLS_SKIP_CHECK=1 TZ=UTC
# Every stamp, FAT and CP/M, is this moment.
at() { faketime '2026-10-26 12:00:00' "$@"; }
# The demo card's, the sources' last commit.
demo_at() { faketime "$DEMO_STAMP" "$@"; }

cat > gen.py <<'PY'
import sys
name, size, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
open(name, 'wb').write(bytes((((i >> 7) * 31 + i * 7 + seed) & 0xFF) for i in range(size)))
PY
# A CP/M text file: CR LF lines, ^Z after the last to the end of its record.
cat > text.py <<'PY'
import sys
name, lines = sys.argv[1], sys.argv[2:]
b = ''.join(l + '\r\n' for l in lines).encode('ascii')
open(name, 'wb').write(b + b'\x1a' * (128 - len(b) % 128))
PY
# mkfs.fat's boot code (dosfstools, GPL) zeroed, as make_sd_images.sh does.
cat > noboot.py <<'PY'
import sys
with open(sys.argv[1], 'r+b') as f:
    f.seek(62)
    f.write(bytes(510 - 62))
PY
cat > sparse.py <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
assert len(data) % 512 == 0
with open(dst, 'w') as f:
    f.write('oo1-sparse 1\nsectors %d\n' % (len(data) // 512))
    for n in range(len(data) // 512):
        s = data[n * 512:(n + 1) * 512]
        if any(s):
            f.write('%d %s\n' % (n, s.hex()))
PY

# OOLDR, OODOS.SYS, the test system, HELLO.COM, WRITEOO1.COM and the
# utilities, from their sources, in a copy laid out as the repo is (/o and /b
# are read-only).
mkdir -p src/oodos src/backplane/bios
cp -r /o/Makefile /o/include /o/sys /o/ldr /o/examples /o/cfg /o/tools /o/utils src/oodos/
cp /b/backplane.inc src/backplane/bios/
rm -rf src/oodos/ldr/build src/oodos/sys/build src/oodos/build
make -s -C src/oodos/ldr
make -s -C src/oodos/sys build/OODOS.SYS build/hello.com build/writeoo1.com
make -s -C src/oodos build/hello.prl
make -s -C src/oodos utils
utils=
for s in src/oodos/utils/*.s; do
  n=$(basename "$s" .s | tr a-z A-Z)
  cp "src/oodos/build/$(basename "$s" .s).com" "$n.COM"
  utils="$utils $n.COM"
done
cp src/oodos/ldr/build/BOOT.BIN BOOT.BIN
cp src/oodos/ldr/build/OODOS.SYS TESTSYS.SYS
cp src/oodos/sys/build/OODOS.SYS OODOS.SYS
cp src/oodos/sys/build/hello.com HELLO.COM
cp src/oodos/build/hello.prl HELLO.PRL
cp src/oodos/sys/build/writeoo1.com WRITEOO1.COM

# A.IMG: made, its label's mode set to 0x31 (oodos/README.md), the files in.
cp /o/diskdefs diskdefs
at mkfs.cpm -f oodos-8m -t -L OODOS A.IMG
# mkfs.cpm writes only the directory, and the file grows as cpmcp writes. A
# unit is its file's size, so the drive gets all 8 MB first; the blocks no
# file has are zeros, as the holes cpmtools would have left read.
truncate -s 8M A.IMG
printf '\061' | dd of=A.IMG bs=1 seek=12 conv=notrunc status=none
python3 text.py README.TXT 'THIS IS AN OODOS DRIVE, MADE ON LINUX BY CPMTOOLS.' \
    'OODOS READS IT AS DRIVE A:, SD UNIT 1, /OODOS/A.IMG ON THE CARD.'
python3 gen.py PATTERN.BIN 36864 1
python3 text.py USER3.TXT 'A FILE IN USER 3.'
python3 gen.py SMALL.BIN 384 3
at cpmcp -f oodos-8m A.IMG README.TXT PATTERN.BIN 0:
at cpmcp -f oodos-8m A.IMG USER3.TXT SMALL.BIN 3:
at cpmcp -f oodos-8m A.IMG OODOS.SYS TESTSYS.SYS HELLO.COM HELLO.PRL WRITEOO1.COM 0:
at cpmcp -f oodos-8m A.IMG $utils 0:
at cpmchattr -f oodos-8m A.IMG s 0:HELLO.COM
for u in $utils; do at cpmchattr -f oodos-8m A.IMG s "0:$u"; done
fsck.cpm -f oodos-8m -n A.IMG
cpmls -f oodos-8m -F A.IMG

# B.IMG: an empty drive, made as A.IMG is.
at mkfs.cpm -f oodos-8m -t -L WORK B.IMG
truncate -s 8M B.IMG
printf '\061' | dd of=B.IMG bs=1 seek=12 conv=notrunc status=none
fsck.cpm -f oodos-8m -n B.IMG

# The card: the partition made alone, then put at 2048.
python3 -c "open('part.img', 'wb').truncate((32 * 2048 - 2048) * 512)"
at mkfs.fat -F 16 -s 4 -i 0001DD05 -n OODOS part.img >/dev/null
python3 noboot.py part.img
at mmd -i part.img ::/OODOS
at mcopy -i part.img BOOT.BIN ::/BOOT.BIN
at mcopy -i part.img /o/card/DRIVES.TXT ::/OODOS/DRIVES.TXT
at mcopy -i part.img A.IMG ::/OODOS/A.IMG
at mcopy -i part.img B.IMG ::/OODOS/B.IMG
mdir -i part.img -/ ::/
python3 -c "open('card.img', 'wb').truncate(32 * 2048 * 512)"
printf 'label: dos\nlabel-id: 0x0001dd05\nstart=2048, type=06\n' | sfdisk -q card.img
dd if=part.img of=card.img bs=512 seek=2048 conv=notrunc status=none
python3 sparse.py card.img /out/oodos.sparse
chown $(stat -c %u /out) /out/oodos.sparse

# The demo card: its drives as the test card's, in demo/.
mkdir demo
demo_at mkfs.cpm -f oodos-8m -t -L OODOS demo/A.IMG
truncate -s 8M demo/A.IMG
printf '\061' | dd of=demo/A.IMG bs=1 seek=12 conv=notrunc status=none
python3 text.py demo/README.TXT \
    "This is the OO-1's demo card: OODOS on drive A:, and drive B:" \
    "empty, to work on. The page makes the card afresh at each visit:" \
    "Settings, SD card, Save keeps what you write." \
    "" \
    "Things to try at A>:" \
    "  DIR             the files on A:" \
    "  DIRS            the programs: HELLO, SHOW, DATE and the rest" \
    "  CLS             clear the screen and list the files; CLEAR only clears" \
    "  TYPE README.TXT this file" \
    "  HELLO           a small program, run from A:" \
    "  SHOW            the drives and their space" \
    "  DATE            the date and time" \
    "  B:              drive B:, and A: to come back" \
    "  WOZMON          leave OODOS for the ROM's Woz Monitor" \
    "  APPLE2          restart as an Apple IIe (your IIe ROM: Settings, ROMs)" \
    "" \
    "START, on the front panel, boots OODOS again. A key during the logo" \
    "at power-on goes to the Woz Monitor instead, and so does a power-on" \
    "with no card."
demo_at cpmcp -f oodos-8m demo/A.IMG demo/README.TXT OODOS.SYS HELLO.COM $utils 0:
demo_at cpmchattr -f oodos-8m demo/A.IMG s 0:HELLO.COM
for u in $utils; do demo_at cpmchattr -f oodos-8m demo/A.IMG s "0:$u"; done
fsck.cpm -f oodos-8m -n demo/A.IMG
cpmls -f oodos-8m -F demo/A.IMG
demo_at mkfs.cpm -f oodos-8m -t -L WORK demo/B.IMG
truncate -s 8M demo/B.IMG
printf '\061' | dd of=demo/B.IMG bs=1 seek=12 conv=notrunc status=none
fsck.cpm -f oodos-8m -n demo/B.IMG
python3 -c "open('demo/part.img', 'wb').truncate((32 * 2048 - 2048) * 512)"
demo_at mkfs.fat -F 16 -s 4 -i 0001DD06 -n 'OODOS DEMO' demo/part.img >/dev/null
python3 noboot.py demo/part.img
demo_at mmd -i demo/part.img ::/OODOS
demo_at mcopy -i demo/part.img BOOT.BIN ::/BOOT.BIN
demo_at mcopy -i demo/part.img /o/card/DRIVES.TXT ::/OODOS/DRIVES.TXT
demo_at mcopy -i demo/part.img demo/A.IMG ::/OODOS/A.IMG
demo_at mcopy -i demo/part.img demo/B.IMG ::/OODOS/B.IMG
mdir -i demo/part.img -/ ::/
python3 -c "open('demo/card.img', 'wb').truncate(32 * 2048 * 512)"
printf 'label: dos\nlabel-id: 0x0001dd06\nstart=2048, type=06\n' | sfdisk -q demo/card.img
dd if=demo/part.img of=demo/card.img bs=512 seek=2048 conv=notrunc status=none
python3 sparse.py demo/card.img /out/demo.sparse
chown $(stat -c %u /out) /out/demo.sparse
INNER
ls -l "$out/oodos.sparse" "$out/demo.sparse"
