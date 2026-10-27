#!/bin/sh
# Makes emulator/test/host/sd/oodos-ram.sparse, what test_oodos_sys checks
# M:'s format against: the directory (8 KB, its 16 sectors) that
# `mkfs.cpm -f oodos-ram -t` writes at 2026-10-27 12:00, with the label's
# mode then set to 0x31, as hw/oodos.md's "Disk format" does. The test sets
# the emulator's clock to that minute before OODOS's cold boot formats M:.
# cpmtools in a throwaway debian:bookworm, pinned by digest, under faketime;
# needs Docker. From anywhere:
#
#   oodos/sys/test/make_mdisk.sh
set -eu
image=debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
oodos="$(cd "$(dirname "$0")/../.." && pwd)"
out="$(cd "$oodos/../emulator/test/host/sd" && pwd)"
docker run --rm -i -v "$(hostpath "$oodos"):/o:ro" -v "$(hostpath "$out"):/out" "$image" sh -eu <<'INNER'
apt-get -qq update >/dev/null && apt-get -qq install -y cpmtools faketime >/dev/null
dpkg-query -W -f '  ${Package} ${Version}\n' cpmtools faketime
cd /o                                   # cpmtools reads ./diskdefs
faketime '2026-10-27 12:00:00' mkfs.cpm -f oodos-ram -t /tmp/m.img
printf '\061' | dd of=/tmp/m.img bs=1 seek=12 conv=notrunc 2>/dev/null
[ "$(stat -c %s /tmp/m.img)" = 8192 ]   # mkfs.cpm writes the directory only
{
  printf 'oo1-sparse 1\nsectors 16\n'
  for s in $(seq 0 15); do
    printf '%d %s\n' "$s" "$(od -An -v -tx1 -j $((s * 512)) -N 512 /tmp/m.img | tr -d ' \n')"
  done
} > /out/oodos-ram.sparse
chown "$(stat -c %u /out)" /out/oodos-ram.sparse
INNER
echo "wrote $out/oodos-ram.sparse"
