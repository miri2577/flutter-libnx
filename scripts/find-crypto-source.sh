#!/usr/bin/env bash
set -u
B="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
echo "=== crypto_* in den .gni-Dateien"
grep -rn "crypto_" "$B"/*.gni
echo
echo "=== crypto_* in BUILD.gn"
grep -n "crypto_" "$B/BUILD.gn"
