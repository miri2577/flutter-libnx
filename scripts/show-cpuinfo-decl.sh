#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime"
echo "########## vm/cpuinfo.h"
cat "$R/vm/cpuinfo.h"
echo
echo "########## vm/native_symbol.h (Deklarationen)"
grep -n "static\|class" "$R/vm/native_symbol.h" | head -14
