#!/usr/bin/env bash
set -u
E="/mnt/e/flutter-libnx/examples/engine_link_test"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
READELF="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-readelf"

echo "=== Artefakte"
ls -lh "$E"/engine_link_test.nro "$E"/engine_link_test.elf 2>/dev/null | awk '{print "  " $5 "  " $9}'

echo
echo "=== NRO0-Magic (Offset 0x10)"
dd if="$E/engine_link_test.nro" bs=1 skip=16 count=4 2>/dev/null | xxd -p | sed 's/^/  /'
dd if="$E/engine_link_test.nro" bs=1 skip=16 count=4 2>/dev/null | sed 's/^/  ASCII: /'
echo

echo "=== ELF-Kopf"
"$READELF" -h "$E/engine_link_test.elf" | grep -E "Class|Machine|Type" | sed 's/^/  /'

echo
echo "=== Engine-Symbole in der ELF"
for sym in FlutterEngineRun FlutterEngineInitialize FlutterEngineGetCurrentTime \
           FlutterEngineRunsAOTCompiledDartCode; do
  if "$NM" --defined-only "$E/engine_link_test.elf" 2>/dev/null | grep -q " T $sym$"; then
    echo "  ok     $sym"
  else
    echo "  fehlt  $sym"
  fi
done

echo
echo "=== Dart-VM im Programm?"
for sym in "dart::Dart::Init" "dart::OS::GetCurrentMonotonicMicros" "dart::VirtualMemory::Init"; do
  if "$NM" --defined-only -C "$E/engine_link_test.elf" 2>/dev/null | grep -q "$sym"; then
    echo "  ok     $sym"
  else
    echo "  fehlt  $sym"
  fi
done
