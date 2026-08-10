#!/usr/bin/env bash
S="$HOME/engine/flutter/engine/src"
echo "=== Inhalt von flutter/buildtools"
ls -la "$S/flutter/buildtools" 2>/dev/null | head -12
echo
echo "=== linux-x64 darin"
ls -la "$S/flutter/buildtools/linux-x64" 2>/dev/null | head -12
echo
echo "=== DEPS-Eintraege zu clang/buildtools"
grep -n "buildtools\|clang" "$HOME/engine/flutter/DEPS" | head -12
