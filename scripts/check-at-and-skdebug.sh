#!/usr/bin/env bash
D="$HOME/devkitpro"
S="$HOME/engine/flutter/engine/src"

echo "=== *at-Funktionen in newlib"
grep -n "openat\|faccessat\|renameat\|AT_FDCWD" \
  "$D/devkitA64/aarch64-none-elf/include/fcntl.h" \
  "$D/devkitA64/aarch64-none-elf/include/unistd.h" \
  "$D/devkitA64/aarch64-none-elf/include/sys/_default_fcntl.h" 2>/dev/null | head -10

echo
echo "=== wer benutzt sie"
grep -rln "openat(\|faccessat(\|renameat(" "$S/flutter/third_party/dart/runtime/bin" 2>/dev/null | head -5

echo
echo "=== SkDebugf-Implementierungen"
ls "$S/flutter/third_party/skia/src/ports/" | grep -i "SkDebug"
echo "--- Auswahl in flutter/skia/BUILD.gn"
grep -n "SkDebug" "$S/flutter/skia/BUILD.gn" | head -8
