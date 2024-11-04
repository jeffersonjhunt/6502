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

FEATURE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Debian / Ubuntu packages
install_debian_packages() {
    # Ensure apt is in non-interactive to avoid prompts
    export DEBIAN_FRONTEND=noninteractive

    local package_list=""
    if [ "${PACKAGES_ALREADY_INSTALLED}" != "true" ]; then
        package_list="${package_list} \
	        locales \
                build-essential"
    fi

    # Install the list of packages
    echo "Packages to verify are installed: ${package_list}"
    rm -rf /var/lib/apt/lists/*
    apt-get update -y
    apt-get -y install --no-install-recommends ${package_list} 2> >( grep -v 'debconf: delaying package configuration, since apt-utils is not installed' >&2 )

    # Install the GNU Debugger (and recommended packages) if needed
    if [ "${INSTALL_GDB}" = "true" ]; then
        apt-get install -y gdb
    fi

    # Install cmake (and recommended packages) if needed
    if [ "${INSTALL_CMAKE}" = "true" ]; then
        apt-get install -y cmake
    fi

    # Install boost (and recommended packages) if needed
    if [ "${INSTALL_BOOST}" = "true" ]; then
        apt-get install -y libboost-all-dev
    fi

    # Install fmt (and recommended packages) if needed
    if [ "${INSTALL_FMT_LIB}" = "true" ]; then
        apt-get install -y libfmt-dev
    fi

    # Install sdl2 (and recommended packages) if needed
    if [ "${INSTALL_SDL2_LIB}" = "true" ]; then
        apt-get install -y libsdl2-dev
    fi

    # Install cc65 (and recommended packages) if needed
    if [ "${INSTALL_CC65}" = "true" ]; then
	apt-get install -y cc65 cc65-doc
    fi

    # Get to latest versions of all packages
    if [ "${UPGRADE_PACKAGES}" = "true" ]; then
        apt-get -y upgrade --no-install-recommends
        apt-get autoremove -y
    fi

    # Ensure at least the en_US.UTF-8 UTF-8 locale is available = common need for applications.
    if [ "${LOCALE_ALREADY_SET}" != "true" ] && ! grep -o -E '^\s*en_US.UTF-8\s+UTF-8' /etc/locale.gen > /dev/null; then
        echo "en_US.UTF-8 UTF-8" >> /etc/locale.gen
        locale-gen
        LOCALE_ALREADY_SET="true"
    fi

    PACKAGES_ALREADY_INSTALLED="true"

    # Clean up
    apt-get -y clean
    rm -rf /var/lib/apt/lists/*
}

# RedHat / RockyLinux / CentOS / Fedora packages
install_redhat_packages() {
    ## TODO ##
    PACKAGES_ALREADY_INSTALLED="true"
}

# Alpine Linux packages
install_alpine_packages() {
    ## TODO ##
    PACKAGES_ALREADY_INSTALLED="true"
}

# ******************
# ** Main section **
# ******************

if [ "$(id -u)" -ne 0 ]; then
    echo -e 'Script must be run as root. Use sudo, su, or add "USER root" to your Dockerfile before running this script.'
    exit 1
fi

# Load markers to see which steps have already run
if [ -f "${MARKER_FILE}" ]; then
    echo "Marker file found:"
    cat "${MARKER_FILE}"
    source "${MARKER_FILE}"
fi

# Ensure that login shells get the correct path if the user updated the PATH using ENV.
rm -f /etc/profile.d/00-restore-env.sh
echo "export PATH=${PATH//$(sh -lc 'echo $PATH')/\$PATH}" > /etc/profile.d/00-restore-env.sh
chmod +x /etc/profile.d/00-restore-env.sh

# Bring in ID, ID_LIKE, VERSION_ID, VERSION_CODENAME
. /etc/os-release
# Get an adjusted ID independent of distro variants
if [ "${ID}" = "debian" ] || [ "${ID_LIKE}" = "debian" ]; then
    ADJUSTED_ID="debian"
elif [[ "${ID}" = "rhel" || "${ID}" = "fedora" || "${ID}" = "mariner" || "${ID_LIKE}" = *"rhel"* || "${ID_LIKE}" = *"fedora"* || "${ID_LIKE}" = *"mariner"* ]]; then
    ADJUSTED_ID="rhel"
elif [ "${ID}" = "alpine" ]; then
    ADJUSTED_ID="alpine"
else
    echo "Linux distro ${ID} not supported."
    exit 1
fi

# Install packages for appropriate OS
case "${ADJUSTED_ID}" in
    "debian")
        install_debian_packages
        ;;
    "rhel")
        install_redhat_packages
        ;;
    "alpine")
        install_alpine_packages
        ;;
esac

## TODO: specific install shell snipits ##

# *****************************
# ** Ensure config directory **
# *****************************
user_config_dir="${user_home}/.config"
if [ ! -d "${user_config_dir}" ]; then
    mkdir -p "${user_config_dir}"
    chown ${USERNAME}:${group_name} "${user_config_dir}"
fi

# ****************************
# ** Utilities and commands **
# ****************************

# code shim, it fallbacks to code-insiders if code is not available
cp -f "${FEATURE_DIR}/bin/code" /usr/local/bin/
chmod +rx /usr/local/bin/code

# systemctl shim for Debian/Ubuntu - tells people to use 'service' if systemd is not running
if [ "${ADJUSTED_ID}" = "debian" ]; then
    cp -f "${FEATURE_DIR}/bin/systemctl" /usr/local/bin/systemctl
    chmod +rx /usr/local/bin/systemctl
fi

# Persist image metadata info, script if meta.env found in same directory
if [ -f "/usr/local/etc/vscode-dev-containers/meta.env" ] || [ -f "/usr/local/etc/dev-containers/meta.env" ]; then
    cp -f "${FEATURE_DIR}/bin/devcontainer-info" /usr/local/bin/devcontainer-info
    chmod +rx /usr/local/bin/devcontainer-info
fi

# Write marker file
if [ ! -d "/usr/local/etc/vscode-dev-containers" ]; then
    mkdir -p "$(dirname "${MARKER_FILE}")"
fi
echo -e "\
    PACKAGES_ALREADY_INSTALLED=${PACKAGES_ALREADY_INSTALLED}\n\
    LOCALE_ALREADY_SET=${LOCALE_ALREADY_SET}" > "${MARKER_FILE}"

echo "Done!"
