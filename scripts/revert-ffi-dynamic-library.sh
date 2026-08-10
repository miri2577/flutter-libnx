#!/usr/bin/env bash
# Setzt ffi_dynamic_library.cc im Dart-Repository zurueck, damit das
# Patch-Skript sauber neu ansetzen kann.
set -eu

D="$HOME/engine/flutter/engine/src/flutter/third_party/dart"
git -C "$D" checkout -- runtime/lib/ffi_dynamic_library.cc
echo "ffi_dynamic_library.cc zurueckgesetzt."
