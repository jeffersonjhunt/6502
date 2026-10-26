#!/bin/sh
# Checks OODOS's diskdefs against cpmtools: each format makes a file system
# that cpmtools writes, lists and checks, with CP/M 3 date stamps (SFCBs) and
# a directory label when made with -t. Runs in a throwaway container:
#
#   docker run --rm -v "$(hostpath oodos):/o:ro" debian:bookworm sh /o/test/check_diskdefs.sh
#
# cpmtools reads ./diskdefs, so it runs in /o. Exits non-zero on the first
# failure; "PASS n" at the end counts the checks.
set -eu
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cpmtools >/dev/null 2>&1
cd /o
work=$(mktemp -d)
passed=0
ok() { passed=$((passed + 1)); echo "ok   $*"; }
fail() { echo "FAIL $*"; exit 1; }

# The byte at a directory entry's offset, as a decimal number.
dirbyte() { od -An -tu1 -j "$(( $2 + $3 ))" -N1 "$1" | tr -d ' '; }

# A diskdef's value: def oodos-8m blocksize.
def() { awk -v f="$1" -v k="$2" '$1 == "diskdef" { on = $2 == f } on && $1 == k { print $2; exit }' diskdefs; }

# What CP/M 3's DPB can hold, which cpmtools doesn't check: the directory's
# blocks fit AL0/AL1 (16 bits), DSM fits 16 bits, and 1 KB blocks need DSM
# under 256 (their EXM can't describe more). The SD drives' ceiling is 512 MB.
dpb() {
  bls=$(def "$1" blocksize); dir=$(def "$1" maxdir)
  bytes=$(( ($(def "$1" tracks) - $(def "$1" boottrk)) * $(def "$1" sectrk) * $(def "$1" seclen) ))
  dsm=$(( bytes / bls - 1 )); dirblk=$(( (dir * 32 + bls - 1) / bls ))
  [ "$dirblk" -le 16 ] || fail "$1: the directory is $dirblk blocks, AL0/AL1 hold 16"
  [ "$dsm" -le 65535 ] || fail "$1: DSM $dsm is over 65535"
  [ "$bls" -ne 1024 ] || [ "$dsm" -lt 256 ] || fail "$1: 1 KB blocks with DSM $dsm (256 or more)"
  [ "$bytes" -le $((512 * 1024 * 1024)) ] || fail "$1: $bytes bytes is over 512 MB"
  if [ "$dsm" -lt 256 ]; then exm=$(( bls / 1024 - 1 )); else exm=$(( bls / 2048 - 1 )); fi
  ok "$1: DPB DSM $dsm, $dirblk directory blocks, EXM $exm"
}

printf 'HELLO FROM LINUX\r\n\032' >"$work/hello.txt"
head -c 20000 /dev/urandom >"$work/big.bin"

for fmt in oodos-8m oodos-32m oodos-ram; do
  img="$work/$fmt.img"
  dpb "$fmt"
  mkfs.cpm -f "$fmt" -t -L OODOS "$img" || fail "$fmt: mkfs.cpm"
  # mkfs.cpm -t sets the label's mode to 0x11 (creation stamps); OODOS's
  # INITDIR sets 0x31 (creation and update), so set that, and check it below.
  # cpmtools writes both stamps whatever the mode: only OODOS's BDOS honours
  # it, and phase 1's interoperability tests check that.
  printf '\061' | dd of="$img" bs=1 seek=12 conv=notrunc 2>/dev/null || fail "$fmt: label mode"
  ok "$fmt: made with time stamps"

  # The directory starts at sector 0 (no boot tracks). Entry 0 is the label
  # (0x20), entry 3 the first SFCB (0x21): one in every four.
  [ "$(dirbyte "$img" 0 0)" = 32 ] || fail "$fmt: entry 0 is not a directory label"
  [ "$(dirbyte "$img" 0 96)" = 33 ] || fail "$fmt: entry 3 is not an SFCB"
  [ "$(dirbyte "$img" 0 224)" = 33 ] || fail "$fmt: entry 7 is not an SFCB"
  [ "$(dirbyte "$img" 0 12)" = 49 ] || fail "$fmt: the label's mode is not 0x31"
  ok "$fmt: a label (creation and update stamps) and an SFCB every fourth entry"

  cpmcp -f "$fmt" "$img" "$work/hello.txt" 0:HELLO.TXT || fail "$fmt: cpmcp in"
  cpmcp -f "$fmt" "$img" "$work/big.bin" 3:BIG.BIN || fail "$fmt: cpmcp in, user 3"
  cpmcp -f "$fmt" "$img" 0:HELLO.TXT "$work/back.txt" || fail "$fmt: cpmcp out"
  cmp -s "$work/hello.txt" "$work/back.txt" || fail "$fmt: HELLO.TXT changed"
  cpmcp -f "$fmt" "$img" 3:BIG.BIN "$work/back.bin" || fail "$fmt: cpmcp out, user 3"
  cmp -s "$work/big.bin" "$work/back.bin" || fail "$fmt: BIG.BIN changed"
  ok "$fmt: files in and out unchanged, users 0 and 3"

  # cpmls -F (CP/M 3's DIR [FULL]) shows cpmcp's two stamps, update and
  # create, both today's; cpmcp writes both whatever the label's mode.
  today=$(date +%m/%d/%y)
  [ "$(cpmls -f "$fmt" -F "$img" | grep '^HELLO ' | grep -o "$today" | wc -l)" = 2 ] || fail "$fmt: HELLO.TXT isn't stamped today"
  # HELLO.TXT is entry 1; its two stamps are the SFCB's (entry 3) bytes 11-18.
  stamps=$(od -An -tx1 -j 107 -N8 "$img" | tr -d ' \n')
  [ "$stamps" != "0000000000000000" ] || fail "$fmt: HELLO.TXT's SFCB stamps are empty"
  ok "$fmt: the file is date stamped"

  fsck.cpm -f "$fmt" -n "$img" >/dev/null || fail "$fmt: fsck.cpm"
  ok "$fmt: fsck.cpm is clean"
done

echo "PASS $passed"
