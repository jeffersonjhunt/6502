#!/bin/sh
# Build and test the WebAssembly target (apps/web) in the emsdk image:
#
#   tools/web_build.sh            # configure, build, ctest --preset web
#   tools/web_build.sh build      # configure and build only
#
# Output: build/web/apps/web/oo1.mjs and oo1.wasm.
#
# The checkout is bind-mounted, so the build is incremental. Bind mounts
# resolve on the Docker daemon's host, not here: from inside a container,
# set HOSTPATH to a command that maps a local path to the daemon host's
# (`hostpath` in a primate does), or run this on the host itself.
set -eu

emulator=$(cd "$(dirname "$0")/.." && pwd)
repo=$(cd "$emulator/.." && pwd)
image=oo1-emsdk:6.0.12
map=${HOSTPATH:-$(command -v hostpath || true)}
mount=$repo
if [ -n "$map" ]; then
    mount=$("$map" "$repo")
fi

docker build -q -t "$image" "$emulator/apps/web" >/dev/null

steps='cmake --preset web && cmake --build --preset web'
if [ "${1:-}" != build ]; then
    steps="$steps && ctest --preset web"
fi

# The emsdk image's cache is root's; a non-root build keeps its own in the tree.
exec docker run --rm --user "$(id -u):$(id -g)" \
    -e HOME=/tmp -e EM_CACHE=/src/emulator/build/.emcache \
    -v "$mount:/src" -w /src/emulator "$image" sh -c "$steps"
