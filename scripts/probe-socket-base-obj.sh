#!/usr/bin/env bash
set -u
OUT="$HOME/engine/flutter/engine/src/out/horizon_release_arm64"
BIN="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"

for o in socket_base_linux socket_linux platform_linux directory_linux file_linux; do
  O="$OUT/obj/flutter/third_party/dart/runtime/bin/common_embedder_dart_io.$o.o"
  if [ -f "$O" ]; then
    N=$("$NM" --defined-only "$O" 2>/dev/null | wc -l)
    printf "  %-22s %5s definierte Symbole  (%s Bytes)\n" "$o" "$N" "$(stat -c%s "$O")"
  else
    printf "  %-22s fehlt\n" "$o"
  fi
done

echo
echo "=== Guards in socket_base_linux.cc"
grep -n "DART_HOST_OS\|^#if\|^#endif\|^#else" "$BIN/socket_base_linux.cc" | head -30
