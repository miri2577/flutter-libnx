#!/usr/bin/env bash
set -u
F="$HOME/engine/flutter/engine/src/flutter/third_party/skia/src/ports/SkFontHost_FreeType.cpp"
echo "=== Zeilen 75-135"
sed -n '75,135p' "$F"
echo
echo "=== alle dlsym/dlopen-Stellen"
grep -n "dlsym\|dlopen\|RTLD" "$F"
