# The OO-1's test toolchain for tools/check.sh, run on a builder instance (tools/aws/README.md): what
# this repo's host has, so a cloud run tests what a local one does. Debian 13 (trixie), as the host:
# gcc 14 with its sanitizers, cmake 3.31, python3, node 20, Debian's cc65 2.19 (the cards' bytes are
# cc65's), the Docker CLI (web_build.sh's emsdk image and the interop stage run beside this one, on the
# instance's daemon), and Playwright's Chromium and WebKit with their libraries. The e2e's packages
# come from package-lock.json at each run (npm ci), so a change to them needs no new image; Playwright's
# browsers are this image's, so its version is pinned here to package.json's.
#
#   docker build -t builder/oo1 tools/ci      (tools/cloud-full.sh --image does this and pushes it)
FROM debian:trixie

ARG PLAYWRIGHT=1.64.1
ENV DEBIAN_FRONTEND=noninteractive \
    PLAYWRIGHT_BROWSERS_PATH=/ms-playwright \
    npm_config_update_notifier=false

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential cmake make python3 cc65 nodejs npm \
        docker-cli git zstd xz-utils curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# The browsers and the system libraries they need, at the version the tests' package.json pins.
RUN npx -y "playwright@${PLAYWRIGHT}" install --with-deps chromium webkit \
 && rm -rf /var/lib/apt/lists/* /root/.npm

RUN cc65 --version && gcc --version | head -1 && cmake --version | head -1 && node --version
