#!/bin/sh
#-------------------------------------------------------------------------------------------------------------------------
# Copyright (c) Jefferson J. Hunt. All rights reserved.
# Licensed under the MIT License. See https://github.com/devcontainers/features/blob/main/LICENSE for license information.
#-------------------------------------------------------------------------------------------------------------------------
#
# Docs: https://github.com/jeffersonjhunt/featured-devcontainers/tree/main/src/cpp
# Maintainer: Jefferson J. Hunt (jeffersonjhunt at gmail.com)

set -e

INSTALL_GDB="${INSTALLGDB:-"true"}"}
INSTALL_BOOST="${INSTALLBOOST:-"true"}"
INSTALL_CMAKE="${INSTALLCMAKE:-"true"}"
INSTALL_FMT_LIB="${INSTALLFMTLIB:-"true"}"
INSTALL_SDL2_LIB="${INSTALLSDL2LIB:-"true"}"
INSTALL_CC65="${INSTALLCC65:-"true"}"

MARKER_FILE="/usr/local/etc/vscode-dev-containers/cpp"

if [ "$(id -u)" -ne 0 ]; then
    echo -e 'Script must be run as root. Use sudo, su, or add "USER root" to your Dockerfile before running this script.'
    exit 1
fi

# If we're using Alpine, install bash before executing
. /etc/os-release
if [ "${ID}" = "alpine" ]; then
    apk add --no-cache bash
fi

exec /bin/bash "$(dirname $0)/main.sh" "$@"
exit $?
