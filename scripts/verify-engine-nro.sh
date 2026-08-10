#!/usr/bin/env bash
set -u
E="/mnt/e/flutter-libnx/examples/engine_link_test"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
READELF="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-readelf"

echo "=== Artefakte"
ls -lh --time-style=+%H:%M "$E"/engine_link_test.nro "$E"/engine_link_test.elf 2>/dev/null \
  | awk '{print "  " $5 "  " $6 "  " $7}'

echo
echo "=== NRO0-Magic (Offset 0x10)"
dd if="$E/engine_link_test.nro" bs=1 skip=16 count=4 2>/dev/null | sed 's/^/  ASCII: /'
echo

echo "=== ELF-Kopf"
"$READELF" -h "$E/engine_link_test.elf" | grep -E "Class|Machine|Type" | sed 's/^/  /'

echo
echo "=== Embedder-API im Programm"
for sym in FlutterEngineInitialize FlutterEngineRunInitialized \
           FlutterEngineSendWindowMetricsEvent FlutterEngineShutdown \
           FlutterEngineGetCurrentTime FlutterEngineRunsAOTCompiledDartCode; do
  if "$NM" --defined-only "$E/engine_link_test.elf" 2>/dev/null | grep -q " T $sym$"; then
    echo "  ok     $sym"
  else
    echo "  fehlt  $sym"
  fi
done

echo
echo "=== Schriften: FreeType und Skias Anbindung"
for sym in "SkTypeface_FreeType" "FT_Init_FreeType" "SkFontMgr_New_Custom_Empty"; do
  N=$("$NM" --defined-only -C "$E/engine_link_test.elf" 2>/dev/null | grep -c "$sym")
  printf "  %-28s %s Symbole\n" "$sym" "$N"
done

echo
echo "=== Weitere Bausteine"
for sym in "dart::Dart::Init" "absl::base_internal::LowLevelAlloc::Alloc" \
           "dart::bin::Process::Init" "fml::MessageLoopHorizon::Run"; do
  if "$NM" --defined-only -C "$E/engine_link_test.elf" 2>/dev/null | grep -q "$sym"; then
    echo "  ok     $sym"
  else
    echo "  fehlt  $sym"
  fi
done

echo
echo "=== Wurzelzertifikate"
"$NM" --defined-only -C "$E/engine_link_test.elf" 2>/dev/null \
  | grep -i "root_certificates" | sed 's/^/  /'
