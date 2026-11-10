#!/bin/sh
# The test tiers: what to run before a build goes to staging, and what before
# it goes to production.
#
#   tools/check.sh quick              # a few minutes: before staging
#   tools/check.sh full               # everything: before production
#   tools/check.sh quick --url URL    # and the e2e smoke set against URL
#   tools/check.sh full --url URL     # and the whole e2e suite against URL
#   tools/check.sh full --jobs 4      # the sanitizer tests 4 at once, not one a CPU (a shared host)
#
# quick: the oodos checks; every native test, built optimised without the
#   sanitizers (the `fast` preset, under a minute); the WebAssembly build
#   and its tests; vitest; the page build; the e2e smoke set (the tests
#   tagged @smoke, a few of each part of the page).
# full: the oodos checks; every native test under ASan/UBSan (the `dev`
#   preset, about 15 minutes); the WebAssembly build and its tests; vitest;
#   the page build; the whole e2e suite; interop, cpmtools reading the cards
#   OODOS writes (Docker).
#
# Every step runs, a failure or not, and the table at the end says which
# failed; the exit status is 1 if any did. A step's output is in
# $CHECK_LOGS (a new temporary directory by default), named for the step.
#
# Needs: cmake, make, python3, node (npm ci done in emulator/apps/web/ui),
# Docker for the WebAssembly build and interop, and the fixtures
# (emulator/tools/fetch_fixtures.py; without them, tests skip: the e2e line
# says how many). The WebAssembly build bind-mounts the checkout, and a bind
# mount resolves on the Docker daemon's host: from inside a container, set
# HOSTPATH to a command that maps a local path to the host's, as
# emulator/tools/web_build.sh takes it. Interop needs no mount.
set -u

tier=${1:-}
url= jobs=all # the sanitizer tests one a CPU; on a host shared with other work, --jobs 4
[ $# -gt 0 ] && shift
while [ $# -gt 0 ]; do
    case $1 in
    --url) url=${2:?--url needs a URL}; shift ;;
    --jobs) jobs=${2:?--jobs needs a number or all}; shift ;;
    *) tier=bad ;;
    esac
    shift
done
[ "$jobs" = all ] && jobs=$(nproc)
case $jobs in '' | *[!0-9]* | 0) tier=bad ;; esac
case $tier in
quick | full) ;;
*)
    echo "usage: tools/check.sh quick|full [--url URL] [--jobs N|all]" >&2
    exit 2
    ;;
esac

repo=$(cd "$(dirname "$0")/.." && pwd)
emu=$repo/emulator
ui=$emu/apps/web/ui
logs=${CHECK_LOGS:-$(mktemp -d)}
mkdir -p "$logs"
map=${HOSTPATH:-$(command -v hostpath || true)} # for web_build.sh's bind mount
summary=$logs/summary
: >"$summary"
unset OO1_BASE_URL # the local build unless --url

# The fixtures (the owner's ROMs, disks and tunes, gitignored; a worktree gets them as links): without them
# the tests that need them skip, and a skip passes, so a run without them would say "ok" having run less.
fixtures=$emu/test/host/fixtures
missing=
for item in 6502_functional_test.bin 65C02_extended_opcodes_test.bin apple2e disks harte rom_images SIDs; do
    if [ ! -e "$fixtures/$item" ] || { [ -d "$fixtures/$item" ] && [ -z "$(ls -A "$fixtures/$item")" ]; }; then
        missing="$missing $item"
    fi
done
if [ -n "$missing" ]; then
    echo "tools/check.sh: not in $fixtures (or empty):$missing" >&2
    echo "  (emulator/tools/fetch_fixtures.py, or links to a checkout that has them)" >&2
    exit 1
fi
failed=0
start=$(date +%s)

# step NAME COMMAND...: runs it in a subshell, its output to $logs/NAME.log.
step() {
    name=$1
    shift
    printf '%-28s ' "$name"
    t0=$(date +%s)
    ("$@") >"$logs/$name.log" 2>&1
    rc=$?
    secs=$(($(date +%s) - t0))
    if [ $rc -eq 0 ]; then
        result=ok
    else
        result="FAILED ($rc)"
        failed=1
    fi
    printf '%-12s %4d s\n' "$result" "$secs"
    printf '%-28s %-12s %4d s\n' "$name" "$result" "$secs" >>"$summary"
}

oodos_checks() {
    make -C "$repo/oodos" check && make -C "$repo/oodos/sys" check &&
        python3 -I -B "$repo/oodos/test/test_model.py" && make -C "$repo/docs" check
}
native() { # $1: the preset
    cd "$emu" && cmake --preset "$1" && cmake --build --preset "$1" -j"$(nproc)" &&
        ctest --preset "$1" -j"${2:-$(nproc)}"
    rc=$?
    rm -f "$emu/stand_in_iie.rom" # test_oodos_utils_rom's own stand-in, if it ran from here
    return $rc
}
wasm() { HOSTPATH=$map "$emu/tools/web_build.sh"; }
vitest() { cd "$ui" && npm test; }
page() { cd "$ui" && OO1_CORE_DIR="$emu/build/web/apps/web" npm run build; }
e2e() { # $1: the page's URL, or empty for the local build; $2: the skips by design; then --grep @smoke, or nothing
    target=$1
    skips=$2
    shift 2
    cd "$ui" || return 1
    if [ -n "$target" ]; then
        OO1_BASE_URL=$target npx playwright test "$@"
    else
        npx playwright test "$@" # OO1_BASE_URL unset, not empty: empty would be a URL of "/"
    fi
    rc=$?
    # The log so far is this run's output (step sends it there).
    skipped=$(grep -oE '^ +[0-9]+ skipped' "$logs/$name.log" | grep -oE '[0-9]+')
    if [ "${skipped:-0}" -gt "$skips" ]; then
        echo "tools/check.sh: $skipped skipped, $skips by design (WebKit has no clipboard or isMobile)"
        return 1
    fi
    return $rc
}
# interop: cpmtools, in a pinned Debian, reads the committed card and the cards test_oodos_card writes as
# OODOS does. oodos/ and the cards go into the container through tar on stdin, not a bind mount: a mount
# resolves on the Docker daemon's host, which may not have this path (a build in a container).
interop() {
    image=$(grep -o 'debian:bookworm@sha256:[0-9a-f]*' "$repo/oodos/tools/make_card.sh" | head -1)
    stage=$logs/interop
    rm -rf "$stage" && mkdir -p "$stage/c" || return 1
    OODOS_CARD_OUT=$stage/c/written.img OODOS_CCP_OUT=$stage/c/ccp.img OODOS_UTILS_OUT=$stage/c/utils.img \
        OODOS_APPLE_OUT=$stage/c/apple.img "$emu/build/$1/test/host/test_oodos_card" || return 1
    cp "$emu/test/host/sd/oodos.sparse" "$stage/c/" || return 1
    for card in "oodos.sparse" "written.img --written" "ccp.img --ccp" "utils.img --utils" "apple.img --apple"; do
        set -- $card
        tar -c --exclude=build -C "$repo" oodos -C "$stage" c |
            docker run --rm -i "$image" sh -c \
                'mkdir /x && tar -x -C /x && ln -s /x/oodos /o && ln -s /x/c /c && sh /o/test/interop.sh "/c/$1" $2' \
                sh "$1" "${2:-}" || return 1
    done
}

echo "tools/check.sh $tier${url:+ --url $url}; logs in $logs"
step oodos-checks oodos_checks
if [ "$tier" = quick ]; then
    preset=fast
    step native-fast native fast
else
    preset=dev
    step native-dev native dev "$jobs"
fi
step wasm wasm
step vitest vitest
step page-build page
if [ "$tier" = quick ]; then
    step e2e-smoke e2e "" 0 --grep @smoke
    if [ -n "$url" ]; then step e2e-smoke-url e2e "$url" 0 --grep @smoke; fi
else
    step e2e e2e "" 2
    if [ -n "$url" ]; then step e2e-url e2e "$url" 2; fi
    step interop interop dev
fi

echo
# A suite that skips a part says SKIP and passes; ctest keeps every test's output in LastTest.log.
last=$emu/build/$preset/Testing/Temporary/LastTest.log
if [ -f "$last" ]; then
    printf '%-28s %s parts skipped\n' "native-$preset" "$(grep -c '^  SKIP' "$last")"
    grep '^  SKIP' "$last" | sort | uniq -c | sed 's/^/    /'
fi
for f in "$logs"/e2e*.log; do
    [ -f "$f" ] && printf '%-28s %s\n' "$(basename "$f" .log)" "$(grep -E '^ +[0-9]+ (passed|failed|skipped|flaky)' "$f" | tr -s ' ' | tr '\n' ',' | sed 's/,$//')"
done
echo "tools/check.sh $tier: $(($(date +%s) - start)) s, $([ $failed -eq 0 ] && echo 'all passed' || echo 'FAILED'); logs in $logs"
exit $failed
