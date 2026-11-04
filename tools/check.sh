#!/bin/sh
# The test tiers: what to run before a build goes to staging, and what before
# it goes to production.
#
#   tools/check.sh quick              # a few minutes: before staging
#   tools/check.sh full               # everything: before production
#   tools/check.sh quick --url URL    # and the e2e smoke set against URL
#   tools/check.sh full --url URL     # and the whole e2e suite against URL
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
# says how many). Docker's bind mounts resolve on the daemon's host: from
# inside a container, set HOSTPATH to a command that maps a local path to
# the host's, as emulator/tools/web_build.sh takes it.
set -u

tier=${1:-}
url=
if [ "${2:-}" = --url ]; then
    url=${3:?--url needs a URL}
fi
case $tier in
quick | full) ;;
*)
    echo "usage: tools/check.sh quick|full [--url URL]" >&2
    exit 2
    ;;
esac

repo=$(cd "$(dirname "$0")/.." && pwd)
emu=$repo/emulator
ui=$emu/apps/web/ui
logs=${CHECK_LOGS:-$(mktemp -d)}
mkdir -p "$logs"
map=${HOSTPATH:-$(command -v hostpath || true)}
summary=$logs/summary
: >"$summary"
unset OO1_BASE_URL # the local build unless --url
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
        ctest --preset "$1" ${2:+-j"$2"}
    rc=$?
    rm -f "$emu/stand_in_iie.rom" # test_oodos_utils_rom's own stand-in, if it ran from here
    return $rc
}
wasm() { HOSTPATH=$map "$emu/tools/web_build.sh"; }
vitest() { cd "$ui" && npm test; }
page() { cd "$ui" && OO1_CORE_DIR="$emu/build/web/apps/web" npm run build; }
e2e() { # $1: the page's URL, or empty for the local build; then --grep @smoke, or nothing
    target=$1
    shift
    cd "$ui" || return 1
    if [ -n "$target" ]; then
        OO1_BASE_URL=$target npx playwright test "$@"
    else
        npx playwright test "$@" # OO1_BASE_URL unset, not empty: empty would be a URL of "/"
    fi
}
# A local path as the Docker daemon's host sees it.
host() {
    if [ -n "$map" ]; then "$map" "$1"; else echo "$1"; fi
}
interop() {
    image=$(grep -o 'debian:bookworm@sha256:[0-9a-f]*' "$repo/oodos/tools/make_card.sh" | head -1)
    out=$logs/cards
    mkdir -p "$out"
    OODOS_CARD_OUT=$out/written.img OODOS_CCP_OUT=$out/ccp.img OODOS_UTILS_OUT=$out/utils.img \
        "$emu/build/$1/test/host/test_oodos_card" || return 1
    o=$(host "$repo/oodos")
    sd=$(host "$emu/test/host/sd")
    c=$(host "$out")
    docker run --rm -v "$o:/o:ro" -v "$sd:/c:ro" "$image" sh /o/test/interop.sh /c/oodos.sparse || return 1
    for m in written ccp utils; do
        docker run --rm -v "$o:/o:ro" -v "$c:/c:ro" "$image" sh /o/test/interop.sh "/c/$m.img" "--$m" || return 1
    done
}

echo "tools/check.sh $tier${url:+ --url $url}; logs in $logs"
step oodos-checks oodos_checks
if [ "$tier" = quick ]; then
    step native-fast native fast
else
    step native-dev native dev 4
fi
step wasm wasm
step vitest vitest
step page-build page
if [ "$tier" = quick ]; then
    step e2e-smoke e2e "" --grep @smoke
    if [ -n "$url" ]; then step e2e-smoke-url e2e "$url" --grep @smoke; fi
else
    step e2e e2e ""
    if [ -n "$url" ]; then step e2e-url e2e "$url"; fi
    step interop interop dev
fi

echo
for f in "$logs"/e2e*.log; do
    [ -f "$f" ] && printf '%-28s %s\n' "$(basename "$f" .log)" "$(grep -E '^ +[0-9]+ (passed|failed|skipped|flaky)' "$f" | tr -s ' ' | tr '\n' ',' | sed 's/,$//')"
done
echo "tools/check.sh $tier: $(($(date +%s) - start)) s, $([ $failed -eq 0 ] && echo 'all passed' || echo 'FAILED'); logs in $logs"
exit $failed
