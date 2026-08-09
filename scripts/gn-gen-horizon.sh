#!/usr/bin/env bash
# Erster gn gen für das noch nicht existierende Target "horizon".
#
# Erwartung beim ersten Lauf: Fehlschlag. Der Zweck ist, die Fehlermeldung zu
# bekommen, statt sie zu erraten.
set -u

SRC="${SRC:-$HOME/engine/flutter/engine/src}"
GN="$SRC/flutter/third_party/gn/gn"
OUT="${OUT:-out/horizon_release_arm64}"

cd "$SRC" || exit 1

echo "==> gn gen $OUT"
echo "    target_os=horizon target_cpu=arm64"
echo

"$GN" gen "$OUT" \
  --args='target_os="horizon" target_cpu="arm64" is_debug=false' \
  2>&1 | tail -40

echo
echo "==> Exitcode der Pipeline: ${PIPESTATUS[0]:-unbekannt}"
