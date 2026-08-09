#!/usr/bin/env bash
# Erster gn gen für das noch nicht existierende Target "horizon".
#
# Erwartung beim ersten Lauf: Fehlschlag. Der Zweck ist, die Fehlermeldung zu
# bekommen, statt sie zu erraten.
set -u

SRC="${SRC:-$HOME/engine/flutter/engine/src}"
GN="$SRC/flutter/third_party/gn/gn"
OUT="${OUT:-out/horizon_release_arm64}"

# GN ruft Hilfsskripte über `vpython3` auf, das aus depot_tools kommt.
export PATH="${DEPOT_TOOLS:-$HOME/depot_tools}:$PATH"
export DEPOT_TOOLS_UPDATE=0

cd "$SRC" || exit 1

echo "==> gn gen $OUT"
echo "    target_os=horizon target_cpu=arm64"
echo

DEVKITPRO="${DEVKITPRO:-$HOME/devkitpro}"
# Diese vier Angaben verlangt //flutter/shell/version/version.gni per assert.
# Sie landen nur in Artefaktnamen und in der Versionsausgabe der Engine, sind
# also für den Build selbst folgenlos – müssen aber gesetzt sein.
ENGINE_VERSION="${ENGINE_VERSION:-db50e20168db8fee486b9abf32fc912de3bc5b6a}"
SKIA_VERSION="${SKIA_VERSION:-a183ded9ad67d998a5b0fe4cd86d3ef5402ffb45}"
DART_VERSION="${DART_VERSION:-02abc57898bebc334a997e609ce5827c8ef207d7}"

"$GN" gen "$OUT" \
  --args="target_os=\"horizon\" target_cpu=\"arm64\" is_debug=false \
devkitpro_root=\"$DEVKITPRO\" engine_version=\"$ENGINE_VERSION\" \
content_hash=\"$ENGINE_VERSION\" skia_version=\"$SKIA_VERSION\" \
dart_version=\"$DART_VERSION\"" \
  2>&1 | tail -40

echo
echo "==> Exitcode der Pipeline: ${PIPESTATUS[0]:-unbekannt}"
