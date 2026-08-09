#!/usr/bin/env bash
# Baut ein Target des Horizon-Engine-Builds.
#
#   build-horizon.sh                  -> baut //flutter/fml
#   build-horizon.sh //flutter/fml    -> baut ein bestimmtes Target
#
# Bewusst nicht der komplette Engine-Build: Erst müssen die unteren Schichten
# übersetzen. fml ist die Betriebssystemabstraktion der Engine und damit die
# erste Schicht, die überhaupt Horizon-spezifisch werden kann.
set -u

SRC="${SRC:-$HOME/engine/flutter/engine/src}"
OUT="${OUT:-out/horizon_release_arm64}"
# Ninja kennt keine GN-Labels mit fuehrendem //, sondern Pfade relativ zum
# Ausgabeverzeichnis.
TARGET="${1:-flutter/fml}"
JOBS="${JOBS:-3}"

export PATH="$HOME/bin:${DEPOT_TOOLS:-$HOME/depot_tools}:$PATH"
export DEPOT_TOOLS_UPDATE=0

NINJA="$SRC/flutter/third_party/ninja/ninja"
if [ ! -x "$NINJA" ]; then
  NINJA="$(command -v ninja || true)"
fi
if [ -z "$NINJA" ] || [ ! -x "$NINJA" ]; then
  echo "ninja nicht gefunden" >&2
  exit 1
fi

cd "$SRC" || exit 1

# Absichtlich wenige Jobs: Der Host hat 4 Kerne und 11 GB, und die Link-Phasen
# der Engine sind speicherhungrig.
echo "==> ninja -C $OUT $TARGET (-j$JOBS)"
"$NINJA" -C "$OUT" -j"$JOBS" "$TARGET"
