#!/usr/bin/env bash
set -u
BIN="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
OUT="$HOME/engine/flutter/engine/src/out/horizon_release_arm64"

echo "=== io_impl_sources.gni: alle *_linux und *_horizon"
grep -n "_linux\|_horizon" "$BIN/io_impl_sources.gni"

echo
echo "=== tatsaechlich gebaute dart_io-Objekte (Plattformdateien)"
find "$OUT/obj" -name "*dart_io*linux*.o" -o -name "*dart_io*horizon*.o" 2>/dev/null \
  | sed "s|$OUT/obj/||" | sort

echo
echo "=== Skia: FreeType-Konfiguration"
grep -n "freetype\|skia_use_freetype\|fontmgr" \
  "$HOME/engine/flutter/engine/src/flutter/skia/BUILD.gn" | head -20

echo
echo "=== gn args"
grep -v "^#" "$OUT/args.gn" | grep -v "^$"
