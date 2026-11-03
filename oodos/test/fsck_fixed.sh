# fsck_fixed: sourced in a test's container (Debian bookworm, oodos at /o),
# after cpmtools is installed. Builds fsck.cpm from Debian's own cpmtools
# source with cpmtools-fsck-stamps.patch and puts it in /usr/local/bin, ahead
# of the package's on PATH; the package's other tools are as they were.
# cpmtools 2.23's fsck.cpm reads an SFCB's first slot's day stamps crosswise
# (the patch says how), and every cpmtools to date is 2.23 (checked
# 2026-11-03: Debian bookworm 2.23-4, trixie and sid 2.23-7, upstream
# www.moria.de/~michael/cpmtools/). Fails the script if the build does.
fsck_fixed() {
  sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
  apt-get -qq update >/dev/null 2>&1
  apt-get -qq install -y dpkg-dev build-essential patch >/dev/null 2>&1
  fsck_src=$(mktemp -d)
  ( cd "$fsck_src" && apt-get -qq source cpmtools >/dev/null 2>&1 && cd cpmtools-* &&
    patch -s -p1 < /o/test/cpmtools-fsck-stamps.patch && ./configure >/dev/null 2>&1 &&
    make fsck.cpm >/dev/null 2>&1 && install -m 755 fsck.cpm /usr/local/bin/fsck.cpm ) ||
    { echo "fsck_fixed: fsck.cpm didn't build with the patch" >&2; exit 1; }
  rm -rf "$fsck_src"
  [ "$(command -v fsck.cpm)" = /usr/local/bin/fsck.cpm ] || { echo "fsck_fixed: not first on PATH" >&2; exit 1; }
}
