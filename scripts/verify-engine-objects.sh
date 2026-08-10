#!/usr/bin/env bash
# Bilanz des Horizon-Engine-Builds.
set -u

OUT="$HOME/engine/flutter/engine/src/out/horizon_release_arm64"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
READELF="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-readelf"

echo "=== Objektdateien gesamt"
find "$OUT/obj" -name "*.o" | wc -l

echo
echo "=== Statische Bibliotheken"
find "$OUT/obj" -name "*.a" | wc -l

echo
echo "=== Groesse des Ausgabeverzeichnisses"
du -sh "$OUT" 2>/dev/null | cut -f1

echo
echo "=== Architektur einer Engine-Objektdatei"
SAMPLE=$(find "$OUT/obj/flutter/shell/platform/embedder" -name "*.o" | head -1)
echo "  $(basename "$SAMPLE")"
"$READELF" -h "$SAMPLE" | grep -E "Class|Machine|Type" | sed 's/^/  /'

echo
echo "=== Kernsymbole der Embedder-API vorhanden?"
for sym in FlutterEngineRun FlutterEngineInitialize FlutterEngineSendWindowMetricsEvent \
           FlutterEngineSendPointerEvent FlutterEngineSendPlatformMessage; do
  if "$NM" --defined-only "$OUT"/obj/flutter/shell/platform/embedder/*.o 2>/dev/null \
      | grep -q " T $sym$"; then
    echo "  ok     $sym"
  else
    echo "  FEHLT  $sym"
  fi
done
