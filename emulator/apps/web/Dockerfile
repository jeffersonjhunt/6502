# The toolchain for the `web` preset: Emscripten, pinned, plus cc65 (the
# BIOS is assembled by the same CMake rules as on the host) and Ninja.
# Built and run by tools/web_build.sh.
FROM emscripten/emsdk:6.0.12
RUN apt-get update \
 && apt-get install -y --no-install-recommends cc65 ninja-build \
 && rm -rf /var/lib/apt/lists/*
