#!/usr/bin/env bash
set -u
D="$HOME/devkitpro/devkitA64/aarch64-none-elf/include"

echo "=== exakte Deklarationen"
grep -rn -A2 "mkdirat\|unlinkat\|faccessat\|renameat *(\|fdopendir" "$D"/sys/stat.h "$D"/unistd.h "$D"/stdio.h "$D"/dirent.h "$D"/sys/dirent.h 2>/dev/null | grep -v "^--"

echo
echo "=== O_DIRECTORY irgendwo?"
grep -rn "O_DIRECTORY" "$D" 2>/dev/null | head
echo "(im Engine-Patchskript:)"
grep -n "O_DIRECTORY" /mnt/e/flutter-libnx/scripts/patch-engine-horizon.py | head

echo
echo "=== was der Engine-Baum daraus macht"
grep -rn "O_DIRECTORY" "$HOME/engine/flutter/engine/src/flutter/fml/" 2>/dev/null | head
