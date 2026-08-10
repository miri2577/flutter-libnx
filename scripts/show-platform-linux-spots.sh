#!/usr/bin/env bash
F="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin/platform_linux.cc"
echo "=== Zeilen 9-24 (Includes)"
sed -n '9,24p' "$F"
echo
echo "=== Umgebung der Treffer"
grep -n -B3 -A6 "utsname\|gethostname\|sysconf\|/proc/" "$F" | head -60
