#!/usr/bin/env bash
# Prueft, welche *at-Funktionen und Verzeichnis-Helfer newlib/libnx anbieten.
set -u
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
LIBDIR="$HOME/devkitpro/devkitA64/aarch64-none-elf/lib"
NXLIB="$HOME/devkitpro/libnx/lib"

echo "=== Definitionen in den Bibliotheken"
for s in mkdirat unlinkat fdopendir dirfd openat renameat faccessat \
         opendir readdir closedir mkdir unlink rmdir rename stat fstat; do
  found=""
  for lib in "$LIBDIR"/libc.a "$LIBDIR"/libsysbase.a "$LIBDIR"/libg.a "$NXLIB"/libnx.a; do
    [ -f "$lib" ] || continue
    if "$NM" --defined-only "$lib" 2>/dev/null | grep -qE "^[0-9a-f]+ +T +$s\$"; then
      found="$found $(basename "$lib")"
    fi
  done
  if [ -n "$found" ]; then
    echo "  ja   $s  <-$found"
  else
    echo "  NEIN $s"
  fi
done
