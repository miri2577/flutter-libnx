#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime"
echo "=== Dateien, die Mutex::Lock definieren"
grep -rln "void Mutex::Lock\|Mutex::Lock()" "$R/platform" "$R/vm" 2>/dev/null | head -5
echo
echo "=== synchronization*-Dateien"
ls "$R/platform" | grep -i "synchron"
echo
echo "=== deren Waechter"
for f in "$R"/platform/synchronization*.cc; do
  [ -e "$f" ] || continue
  echo "--- $(basename "$f")"
  grep -n "DART_HOST_OS" "$f" | head -3
done
