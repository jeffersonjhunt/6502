#!/bin/sh
# Builds the FAT card images test_sd_fat checks the SD card's firmware
# against, with the real tools (mkfs.fat, mtools, sfdisk) rather than our
# own writer, so the reader is checked against what a PC makes. Run from
# emulator/; needs Docker. Writes test/host/sd/*.sparse: each image's
# non-zero sectors as hex, which the test expands (most of a card is 0).
#
#   tools/make_sd_images.sh
#
# A file's byte i is ((i >> 9) * 31 + i * 7 + seed) & 0xFF, its seed in
# the comments below; test_sd_fat.cpp knows the same.
set -eu
# debian:bookworm as of 2026-10-20, as oodos/tools/make_card.sh.
image=debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
out="$(cd "$(dirname "$0")/.." && pwd)/test/host/sd"
mkdir -p "$out"
docker run --rm -i -v "$(hostpath "$out"):/out" "$image" sh -eu <<'INNER'
apt-get -qq update >/dev/null && apt-get -qq install -y dosfstools mtools fdisk python3 >/dev/null
cd /tmp
cat > gen.py <<'PY'
import sys
name, size, seed = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
open(name, 'wb').write(bytes((((i >> 9) * 31 + i * 7 + seed) & 0xFF) for i in range(size)))
PY
# mkfs.fat writes its own boot code (dosfstools, GPL) into each boot sector:
# zero it, so nothing of it lands in this repo. Nothing here reads it.
cat > noboot.py <<'PY'
import sys
path, fat32 = sys.argv[1], sys.argv[2] == '32'
with open(path, 'r+b') as f:
    for sector in ([0, 6] if fat32 else [0]):  # FAT32 keeps a backup at 6
        start = 90 if fat32 else 62
        f.seek(sector * 512 + start)
        f.write(bytes(510 - start))
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
export MTOOLS_SKIP_CHECK=1

# FAT12, a superfloppy (no partition table), 1440 KB.
mkfs.fat -C -F 12 -i 0001F12E -n OO1FAT12 fat12.img 1440 >/dev/null
python3 noboot.py fat12.img 12
printf 'Hello from the OO-1\r\n' > HELLO.TXT
python3 gen.py long.dsk 20000 1          # seed 1
python3 gen.py GAME.PO 16384 2           # seed 2
python3 gen.py README 600 3              # seed 3
mcopy -i fat12.img HELLO.TXT ::/HELLO.TXT
mcopy -i fat12.img long.dsk "::/A long file name.dsk"
mmd -i fat12.img ::/DISKS
mcopy -i fat12.img GAME.PO ::/DISKS/GAME.PO
mcopy -i fat12.img README ::/DISKS/README
mattrib -i fat12.img +r ::/DISKS/README
# BOOT.BIN, for the BIOS's C80CR: at $1000, it prints through $C80F (ECHO) and
# returns to the monitor. One sector:
#   LDX #0 / LDA MSG,X / BEQ done / JSR $C80F / INX / BNE / done: RTS / MSG
python3 - <<'PY'
code = bytes.fromhex('A200BD0E10F006200FC8E8D0F560') + b'\rBOOTED FROM SD\r\0'
open('BOOT.BIN', 'wb').write(code + bytes(512 - len(code)))
PY
mcopy -i fat12.img HELLO.TXT "::/This name is longer than the fifty-six bytes a listing keeps of it.txt"
mcopy -i fat12.img BOOT.BIN ::/BOOT.BIN
python3 sparse.py fat12.img /out/fat12.sparse

# FAT16 in partition 1 (type 06) at sector 2048 of a 20 MB card, 2 KB clusters.
python3 - <<'PY'
open('part16.img', 'wb').truncate((20 * 2048 - 2048) * 512)
PY
mkfs.fat -F 16 -s 4 -i 0001F16E -n OO1FAT16 part16.img >/dev/null
python3 noboot.py part16.img 16
python3 gen.py A.BIN 10240 9
python3 gen.py B.BIN 10240 9
python3 gen.py FRAG.BIN 40960 4          # seed 4: fragmented, into A's hole then after B
python3 gen.py ODD.BIN 1000 5            # seed 5: not whole sectors
mcopy -i part16.img A.BIN ::/A.BIN
mcopy -i part16.img B.BIN ::/B.BIN
mdel -i part16.img ::/A.BIN
mcopy -i part16.img FRAG.BIN ::/FRAG.BIN
mcopy -i part16.img ODD.BIN ::/ODD.BIN
python3 - <<'PY'
open('fat16.img', 'wb').truncate(20 * 2048 * 512)
PY
echo 'start=2048, type=06' | sfdisk -q fat16.img
dd if=part16.img of=fat16.img bs=512 seek=2048 conv=notrunc status=none
python3 sparse.py fat16.img /out/fat16.sparse

# FAT32 in partition 1 (type 0C) at 2048 of a 40 MB card, 512-byte clusters,
# so a directory's 16 entries fill a cluster and its chain matters.
python3 - <<'PY'
open('part32.img', 'wb').truncate((40 * 2048 - 2048) * 512)
PY
mkfs.fat -F 32 -s 1 -i 0001F32E -n OO1FAT32 part32.img >/dev/null
python3 noboot.py part32.img 32
python3 gen.py DOS33.DSK 16384 6         # seed 6
mmd -i part32.img ::/IMAGES ::/IMAGES/APPLE ::/MANY
mcopy -i part32.img DOS33.DSK ::/IMAGES/APPLE/DOS33.DSK
i=1
while [ $i -le 40 ]; do
    n=$(printf %02d $i)
    printf 'file %s\r\n' $n > f.txt
    if [ $((i % 2)) -eq 0 ]; then
        mcopy -i part32.img f.txt "::/MANY/Entry number $n with a long name.txt"
    else
        mcopy -i part32.img f.txt "::/MANY/FILE$n.TXT"
    fi
    i=$((i + 1))
done
python3 - <<'PY'
open('fat32.img', 'wb').truncate(40 * 2048 * 512)
PY
echo 'start=2048, type=0c' | sfdisk -q fat32.img
dd if=part32.img of=fat32.img bs=512 seek=2048 conv=notrunc status=none
python3 sparse.py fat32.img /out/fat32.sparse
chown $(stat -c %u /out) /out/*.sparse
INNER
ls -l "$out"
