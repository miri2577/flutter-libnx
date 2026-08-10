#!/usr/bin/env bash
S="$HOME/engine/flutter/engine/src"
echo "=== tatsaechlicher clang++"
ls -l "$S/flutter/buildtools/linux-x64/clang/bin/clang++" 2>/dev/null || echo "  fehlt auch dort"
echo
echo "=== clang_base_path Definitionen"
grep -rn "clang_base_path" "$S/build" --include=*.gni --include=*.gn 2>/dev/null | head -10
echo
echo "=== Ueberschreibung im flutter-Baum"
grep -rn "clang_base_path" "$S/flutter" --include=*.gni --include=*.gn 2>/dev/null | head -10
