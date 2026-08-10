#!/usr/bin/env bash
S="$HOME/engine/flutter/engine/src"
echo "=== buildtools_path im gesamten Baum (ohne third_party)"
grep -rn "buildtools_path" "$S/build" "$S/build_overrides" "$S/flutter/build" 2>/dev/null | head -10
echo
echo "=== wie build/toolchain/linux clang findet"
grep -n "clang\|prefix\|buildtools" "$S/build/toolchain/linux/BUILD.gn" | head -20
