#!/usr/bin/env bash
# Holt GNU make ohne root nach ~/bin.
#
# Die WSL-Installation ist blank; make fehlt ebenso wie zuvor pkg-config.
# Quelle ist wieder das devkitPro-Image, das ohnehin lokal liegt.
set -eu

TAR="/mnt/e/make.tar"
BIN="$HOME/bin"

mkdir -p "$BIN"
rm -rf /tmp/makex
mkdir -p /tmp/makex
tar -xf "$TAR" -C /tmp/makex

cp -a /tmp/makex/usr/bin/make "$BIN/"
echo "installiert nach $BIN"
"$BIN/make" --version | head -1
