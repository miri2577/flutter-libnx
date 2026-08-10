#!/usr/bin/env bash
# Wer haengt noch -ldl an? libnx hat kein libdl.
S="$HOME/engine/flutter/engine/src"
echo '=== "dl" in libs-Listen (ohne third_party/dart/tools)'
grep -rn 'libs *= *\[ *"dl"\|libs += \[ *"dl"\|"dl",' "$S/flutter" "$S/build" \
  --include=*.gn --include=*.gni 2>/dev/null | head -12
echo
echo "=== im erzeugten Link-Kommando"
grep -o '\-ldl' "$S/out/horizon_release_arm64/libflutter_engine.so.rsp" 2>/dev/null | head -2
echo
echo "=== ninja-Regel fuer das Ziel"
grep -n "ldl\|-ldl" "$S/out/horizon_release_arm64/build.ninja" 2>/dev/null | head -5
