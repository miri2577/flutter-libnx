#!/usr/bin/env bash
set -u
OUT="$HOME/engine/flutter/engine/src/out/horizon_release_arm64"
LIB="$OUT/obj/flutter/shell/platform/embedder/libflutter_engine.a"
AR="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-ar"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"

echo "=== Archiv"
ls -lh "$LIB" | awk '{print "  " $5 "  " $9}'
echo "  Mitglieder: $("$AR" t "$LIB" | wc -l)"

echo
echo "=== dart_io-Mitglieder mit 'socket' im Namen"
"$AR" t "$LIB" | grep -i "socket" | sed 's/^/  /'

echo
echo "=== Wer definiert SocketBase::Read?"
"$NM" -C --defined-only "$LIB" 2>/dev/null | grep -n "SocketBase::Read" | head -3
echo "  (leer heisst: niemand)"

echo
echo "=== Wer definiert es im Objekt direkt?"
O="$OUT/obj/flutter/third_party/dart/runtime/bin/common_embedder_dart_io.socket_base_linux.o"
"$NM" -C --defined-only "$O" 2>/dev/null | grep "SocketBase::" | head -5

echo
echo "=== Ist genau dieses Objekt im Archiv?"
if "$AR" t "$LIB" | grep -q "common_embedder_dart_io.socket_base_linux.o"; then
  echo "  ja"
else
  echo "  NEIN - das Archiv kennt die Datei nicht"
fi
