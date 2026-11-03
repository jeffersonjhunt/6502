# What check_diskdefs.sh and interop.sh share, sourced by both: a diskdef's
# values, and fsck.cpm judged by its report. The caller defines fail.

# A diskdef's value: def oodos-8m blocksize. Reads ./diskdefs.
def() { awk -v f="$1" -v k="$2" '$1 == "diskdef" { on = $2 == f } on && $1 == k { print $2; exit }' diskdefs; }

# fsck_clean FORMAT IMAGE [ALLOWED]: fsck.cpm -n, printed, must exit 0 and
# report no Error or Warning lines. fsck.cpm exits 0 after some errors (a
# bad stamp, for one), so its report is what counts. ALLOWED, a file, lists
# lines to let through, as the caller's own checks work them out (sfcb.py's,
# modeltest.py's). fsck.cpm is fsck_fixed.sh's, cpmtools 2.23's with its
# first-slot stamps read straight.
fsck_clean() {
  out=$(fsck.cpm -f "$1" -n "$2" 2>&1) || { echo "$out"; fail "$1: fsck.cpm exits non-zero"; }
  echo "$out" | sed 's/^/     /'
  bad=$(echo "$out" | grep -E '^(Error|Warning)' | grep -vxF -f "${3:-/dev/null}" || true)
  [ -z "$bad" ] || fail "$1: fsck.cpm reports: $bad"
}
