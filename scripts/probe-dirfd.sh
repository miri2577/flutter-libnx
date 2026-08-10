#!/usr/bin/env bash
# Klaert, ob libnx' devoptab ein Verzeichnis als Dateideskriptor oeffnen kann.
set -u
NX="$HOME/devkitpro/libnx"
SRC=""
for c in "$HOME/libnx/nx/source/runtime/devices/fs_dev.c" \
         "$HOME/src/libnx/nx/source/runtime/devices/fs_dev.c"; do
  [ -f "$c" ] && SRC="$c" && break
done

echo "=== libnx-Quelle"
if [ -n "$SRC" ]; then
  echo "  $SRC"
else
  echo "  kein Quell-Checkout gefunden"
fi

echo
echo "=== dirent-Struktur (dirent.h)"
sed -n '1,60p' "$HOME/devkitpro/devkitA64/aarch64-none-elf/include/sys/dirent.h" 2>/dev/null

echo
echo "=== DIR-Struktur (libsysbase iosupport.h)"
grep -n "struct DIR_ITER\|dirStateSize\|diropen_r\|dirnext_r" \
  "$NX/include"/*.h "$HOME/devkitpro/devkitA64/aarch64-none-elf/include/sys/iosupport.h" 2>/dev/null | head -20

echo
echo "=== open_r-Signatur im devoptab"
grep -n "open_r\|fstat_r\|dirstatesize" \
  "$HOME/devkitpro/devkitA64/aarch64-none-elf/include/sys/iosupport.h" 2>/dev/null | head -20

if [ -n "$SRC" ]; then
  echo
  echo "=== fs_dev open: Verzeichnis-Behandlung"
  grep -n "EISDIR\|FsDirEntryType_Dir\|O_DIRECTORY" "$SRC" | head -20
fi
