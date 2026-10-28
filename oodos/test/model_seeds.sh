#!/bin/sh
# OODOS's BDOS against the model on many seeds, a test_oodos_model process a seed, in parallel
# (hw/oodos.md, "The BDOS, phase 3"; oodos/README.md). From emulator/:
#
#   sh ../oodos/test/model_seeds.sh [-j N] [-o LOGS] ./build/dev/test/host/test_oodos_model 1-600 [7 9-12 ...]
#
# Each seed runs as OODOS_MODEL_ONLY=1 OODOS_MODEL_SEEDS=n (the other OODOS_MODEL_ settings pass
# through) into LOGS/seedN.log (default: a new directory under /tmp). A seed passes only if its process
# exits 0 and its log has "seed N: PASS": a hang cut short, a crash, an ASan report, a FAIL, no
# RESULT.BIN, a build with the comparison off, all fail. Prints a line for each seed that failed, then
# "passed/seeds seeds pass"; exits 0 only if every seed passed. -j: processes at once (default: the
# processors).
set -u
jobs=$(nproc 2>/dev/null || echo 4)
logs=""
while getopts j:o: opt; do
  case $opt in
    j) jobs=$OPTARG ;;
    o) logs=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -ge 2 ] || { echo "usage: $0 [-j N] [-o LOGS] TEST_OODOS_MODEL SEED|LO-HI..." >&2; exit 2; }
bin=$1
shift
[ -x "$bin" ] || { echo "$bin: not a program" >&2; exit 2; }
[ -n "$logs" ] || logs=$(mktemp -d /tmp/model_seeds.XXXXXX)
mkdir -p "$logs"

seeds=""
for s in "$@"; do
  case $s in
    *[!0-9-]* | -* | *- | *-*-*) echo "$s: not a seed or LO-HI" >&2; exit 2 ;;
    *-*) seeds="$seeds $(seq "${s%-*}" "${s#*-}")" ;;
    *) seeds="$seeds $s" ;;
  esac
done
total=$(echo $seeds | wc -w)
[ "$total" -gt 0 ] || { echo "no seeds" >&2; exit 2; }

# A seed's exit status goes to seedN.rc after it ends: no .rc, it didn't.
rm -f "$logs"/seed*.rc
echo $seeds | tr ' ' '\n' | xargs -P "$jobs" -I{} sh -c '
  OODOS_MODEL_ONLY=1 OODOS_MODEL_SEEDS={} "$1" >"$2/seed{}.log" 2>&1
  echo $? >"$2/seed{}.rc"' sh "$bin" "$logs"

passed=0
for s in $seeds; do
  rc=$(cat "$logs/seed$s.rc" 2>/dev/null || echo "none")
  if [ "$rc" = 0 ] && grep -q "^  seed $s: PASS" "$logs/seed$s.log"; then
    passed=$((passed + 1))
  else
    why=$(grep -E "seed $s: FAIL|^FAIL|NOT OK|ERROR: AddressSanitizer|runtime error|skipped" "$logs/seed$s.log" | head -3 | tr '\n' ' ')
    echo "FAIL seed $s: exit $rc: ${why:-$(tail -1 "$logs/seed$s.log" 2>/dev/null)} ($logs/seed$s.log)"
  fi
done
echo "$passed/$total seeds pass (logs: $logs)"
[ "$passed" = "$total" ]
