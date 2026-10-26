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

printf 'HELLO FROM LINUX\r\n\032' >"$work/hello.txt"
head -c 20000 /dev/urandom >"$work/big.bin"

for fmt in oodos-8m oodos-32m oodos-ram; do
  img="$work/$fmt.img"
  mkfs.cpm -f "$fmt" -t -L OODOS "$img" || fail "$fmt: mkfs.cpm"
  # mkfs.cpm -t keeps creation stamps only (mode 0x11); OODOS keeps creation
  # and update (0x31), as its INITDIR does.
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

  # cpmls -F (CP/M 3's DIR [FULL]) shows the stamps it wrote: today's, update and create.
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
