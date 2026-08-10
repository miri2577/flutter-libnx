#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src"

echo "=== flutter/skia/BUILD.gn, Kopf der Konfiguration"
sed -n '1,60p' "$S/flutter/skia/BUILD.gn"

echo
echo "=== low_level_alloc.h: Bedingung fuer ABSL_LOW_LEVEL_ALLOC_MISSING"
sed -n '25,50p' "$S/third_party/abseil-cpp/absl/base/internal/low_level_alloc.h"
