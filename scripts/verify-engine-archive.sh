#!/usr/bin/env bash
set -u
OUT="$HOME/engine/flutter/engine/src/out/horizon_release_arm64"
A="$OUT/obj/flutter/shell/platform/embedder/libflutter_engine.a"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"

echo "=== Archiv"
ls -lh "$A" | awk '{print "  " $5 "  " $9}'
echo "  Objektdateien darin: $("$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-ar" t "$A" | wc -l)"

echo
echo "=== Embedder-API im Archiv"
for sym in FlutterEngineRun FlutterEngineInitialize FlutterEngineRunInitialized \
           FlutterEngineSendWindowMetricsEvent FlutterEngineSendPointerEvent \
           FlutterEngineSendPlatformMessage FlutterEngineShutdown \
           FlutterEngineRunsAOTCompiledDartCode; do
  if "$NM" --defined-only "$A" 2>/dev/null | grep -q " T $sym$"; then
    echo "  ok     $sym"
  else
    echo "  FEHLT  $sym"
  fi
done
