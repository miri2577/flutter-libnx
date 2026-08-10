#!/usr/bin/env bash
set -u
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
E="/mnt/e/flutter-libnx/examples/engine_link_test/engine_link_test.elf"

echo "=== MessageLoopHorizon im Programm"
"$NM" -C --defined-only "$E" | grep "MessageLoopHorizon::" | sed 's/^/  /'

echo
echo "=== keine Linux-Schleife hineingeraten?"
if "$NM" -C --defined-only "$E" | grep -q "MessageLoopLinux::"; then
  echo "  ACHTUNG: MessageLoopLinux ist ebenfalls im Programm"
else
  echo "  ok, MessageLoopLinux fehlt wie erwartet"
fi

echo
echo "=== Groesse"
ls -lh "$E" "${E%.elf}.nro" | awk '{print "  " $5 "  " $9}'
