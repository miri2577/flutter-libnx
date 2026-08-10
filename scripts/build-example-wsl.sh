#!/usr/bin/env bash
# Baut ein Beispiel in WSL statt im Container.
#
# Noetig, sobald ein Beispiel gegen die Engine linkt: Der Engine-Checkout liegt
# im WSL-Dateisystem und ist im Container nicht sichtbar. devkitA64 wurde
# dafuer ohnehin schon nach ~/devkitpro entpackt.
#
#   build-example-wsl.sh engine_link_test
set -eu

EXAMPLE="${1:-engine_link_test}"
shift || true   # der Rest geht an make weiter, der Name nicht
REPO="${REPO:-/mnt/e/flutter-libnx}"

export DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"
export DEVKITA64="$DEVKITPRO/devkitA64"
# ~/bin enthaelt die ohne root beschafften Werkzeuge (make, pkg-config).
export PATH="$HOME/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
export ENGINE_SRC="${ENGINE_SRC:-$HOME/engine/flutter/engine/src}"
export ENGINE_OUT="${ENGINE_OUT:-$ENGINE_SRC/out/horizon_release_arm64}"

cd "$REPO/examples/$EXAMPLE"
echo "==> make in $(pwd)"
echo "    DEVKITPRO=$DEVKITPRO"
echo "    ENGINE_OUT=$ENGINE_OUT"
make "$@"
