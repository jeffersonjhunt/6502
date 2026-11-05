#!/bin/sh
# Build the WebAssembly target inside the emsdk image by streaming the
# checkout over stdin (no bind mount; the daemon's host does not share this
# path), then stream the artifacts back as base64. Used when
# tools/web_build.sh's bind mount is unavailable (the host's file sharing
# does not include this checkout).
#
#   tools/wasm_build_stdin.sh <outdir> [profile]
#
#   outdir    host directory to write build/web/apps/web/oo1.mjs and oo1.wasm to
#   profile   add the EMU_PROFILE flag (Machine.profile() export)
#
set -eu

emulator=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$emulator/.." && pwd)
outdir=${1:?usage: wasm_build_stdin.sh <outdir> [profile]}
profile=${2:-}
mkdir -p "$outdir"

# The repo, tarred, in. The build log, and the two artifacts, out.
tar -C "$repo" -cf - \
    --exclude='build' --exclude='node_modules' --exclude='.git' . |
docker run --rm -i oo1-emsdk:6.0.12 sh -c "
    set -e
    mkdir -p /src && tar -xf - -C /src
    cd /src/emulator
    cmake --preset web -D CMAKE_BUILD_TYPE=Release ${profile:+-D EMU_PROFILE=ON} >/dev/null
    cmake --build --preset web -j \"\$(nproc)\" 2>&1
    echo '===ART:oo1.mjs==='
    base64 -w0 build/web/apps/web/oo1.mjs
    echo
    echo '===ART:oo1.wasm==='
    base64 -w0 build/web/apps/web/oo1.wasm
    echo
    echo '===END==='
" > /tmp/wasm_build_out.txt 2>&1
status=$?

# Pull the two artifacts out by marker.
state=
while IFS= read -r line; do
    case "$line" in
        "===ART:oo1.mjs===") state=mjs ;;
        "===ART:oo1.wasm===") state=wasm ;;
        "===END===") state= ;;
        *) [ -n "$state" ] && printf '%s\n' "$line" > "/tmp/wasm_b64_$state" ;;
    esac
done < /tmp/wasm_build_out.txt
base64 -d /tmp/wasm_b64_mjs  > "$outdir/oo1.mjs"
base64 -d /tmp/wasm_b64_wasm > "$outdir/oo1.wasm"

# The log is everything before the first marker.
sed -n '1,/^===ART:/{/^===ART:/d;p}' /tmp/wasm_build_out.txt | tail -40

exit "$status"
