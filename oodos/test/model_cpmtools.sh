#!/bin/sh
# Checks the model's drive images with cpmtools (hw/oodos.md, "The BDOS, phase 3"): what the model
# (cpm3model.py) leaves after a seed's script must be a drive cpmtools reads as the model does. In a
# throwaway container:
#
#   docker run --rm -v "$(hostpath oodos):/o:ro" \
#       debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61 \
#       sh /o/test/model_cpmtools.sh [SEED...]
#
# First the model's empty drives against mkfs.cpm -t's at the same minute (faketime), the label's mode
# set to 0x31: the directory byte for byte, for each format. Then, for each seed (1-6 by default; its
# format by seed mod 3, as test_oodos_model's: oodos-ram, oodos-8m, oodos-32m), modeltest.py's run of the
# script, and on the drive it leaves: the stamps' order (sfcb.py), fsck.cpm -n (no Error or Warning
# lines but fsck.cpm 2.23's first-slot one, interop.sh's way, its "Bad record count" for exactly
# the entries whose last extent has a hole, which it takes for damage, and fails on an entry with a
# block past its RC's: modeltest.py sparse; and its "Bit 4 and 6" for a label asking for creation and
# access stamps, which the Guide allows: modeltest.py label. fsck.cpm decodes each XFCB's password, and
# the label's with passwords on, and fails one that isn't printable), cpmls -F
# (every file, with its first
# entry's update and create stamps and its password mode) and every file's contents by cpmcp (a hole reads as zeros). Exits
# non-zero on the first failure; "PASS n" at the end counts the checks.
set -eu
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cpmtools faketime python3 >/dev/null 2>&1
dpkg-query -W -f '${Package} ${Version}\n' cpmtools faketime python3 | sed 's/^/     /'
cd /o
. "$(dirname "$0")/cpmtools.sh"
work=$(mktemp -d)
passed=0
ok() { passed=$((passed + 1)); echo "ok   $*"; }
fail() { echo "FAIL $*"; exit 1; }
model() { python3 -I test/modeltest.py "$@"; }

# fsck_clean's check, but fsck.cpm exits 1 after a "Bad record count": then it must have printed one,
# and every Error or Warning line must be one ALLOWED lists.
fsck_model() {
  rc=0
  out=$(fsck.cpm -f "$1" -n "$2" 2>&1) || rc=$?
  echo "$out" | sed 's/^/     /'
  bad=$(echo "$out" | grep -E '^(Error|Warning)' | grep -vxF -f "$3" || true)
  [ -z "$bad" ] || fail "$1: fsck.cpm reports: $bad"
  [ "$rc" = 0 ] || echo "$out" | grep -q -e '^Error: Bad record count' -e '^Error: Bit 4 and 6' ||
    fail "$1: fsck.cpm exits $rc"
}

for f in oodos-ram oodos-8m oodos-32m; do
  faketime '2026-10-27 12:00:00' mkfs.cpm -f "$f" -t "$work/$f.img"
  printf '\061' | dd of="$work/$f.img" bs=1 seek=12 conv=notrunc 2>/dev/null
  model mkfs --format "$f" -o "$work/$f.model"
  n=$(($(def "$f" maxdir) * 32))
  cmp -n "$n" "$work/$f.img" "$work/$f.model" || fail "$f: the model's empty directory isn't mkfs.cpm -t's"
  ok "$f: the model's empty directory is mkfs.cpm -t's, $n bytes"
done

formats="oodos-ram oodos-8m oodos-32m"
for seed in ${*:-1 2 3 4 5 6}; do
  f=$(echo $formats | cut -d' ' -f$((seed % 3 + 1)))
  d="$work/seed$seed"
  model prepare --seed "$seed" --format "$f" --dir "$d" | sed 's/^/     /'
  model predict --dir "$d" >/dev/null
  img="$d/T.model.IMG"
  # A label set by the script (100) may have asked for other stamps: then no order is checked.
  any=$(model show --dir "$d" | grep -q 'FCALL 100 ' && echo any || true)
  order=$(python3 -I test/sfcb.py "$img" "$(def "$f" maxdir)" check "$d/allowed" $any) || { echo "$order"; fail "seed $seed: sfcb.py"; }
  model sparse --image "$img" --format "$f" >>"$d/allowed" || fail "seed $seed: an RC too small for its blocks"
  model label --image "$img" --format "$f" >>"$d/allowed"
  fsck_model "$f" "$img" "$d/allowed"
  listing=$(cpmls -f "$f" -F "$img") || fail "seed $seed: cpmls -F"
  echo "$listing" | sed 's/^/     /'
  model files --image "$img" --format "$f" --out "$d/files" >"$d/manifest"
  files=0
  prots=0
  while read -r user name ext upd_d upd_t cre_d cre_t prot; do
    [ "$ext" = - ] && ext=""
    file=$name${ext:+.$ext}
    # cpmls -F: "Directory For Drive A:  User n", then a line a file: name, type, ..., the password
    # mode (Prot, from the slot), update, create.
    echo "$listing" | awk -v u="$user" -v n="$name" -v e="$ext" -v s="$upd_d $upd_t  $cre_d $cre_t" -v p="$prot" '
      /^Directory For Drive/ { cur = $NF + 0 }
      cur == u + 0 && $1 == n && (e == "" || $2 == e) && index($0, sprintf("%-7s", p) s) { found = 1 }
      END { exit !found }' ||
      fail "seed $seed: cpmls -F hasn't $user:$file $prot, updated $upd_d $upd_t, created $cre_d $cre_t"
    [ "$prot" = None ] || prots=$((prots + 1))
    cpmcp -f "$f" "$img" "$user:$file" "$d/out" || fail "seed $seed: cpmcp $user:$file"
    cmp -s "$d/files/$user/$file" "$d/out" || fail "seed $seed: $user:$file by cpmcp isn't the model's"
    files=$((files + 1))
  done <"$d/manifest"
  ok "seed $seed ($f): fsck.cpm clean, $files files listed with their stamps and modes ($prots protected), copied out as the model has them"
done

echo "PASS $passed"
