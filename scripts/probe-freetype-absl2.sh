#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src"
OUT="$S/out/horizon_release_arm64"

echo "=== FreeType-Ports von Skia (SkFontHost_FreeType)"
find "$OUT/obj" -name "*FreeType*" -o -name "*freetype*.o" 2>/dev/null \
  | grep -i "skia\|fonthost" | head -5
echo "  Treffer: $(find "$OUT/obj/flutter/third_party/skia" -iname "*freetype*" 2>/dev/null | wc -l)"
echo
echo "  skia_ports_freetype_sources:"
grep -rn "skia_ports_freetype_sources" "$S/flutter/third_party/skia/gn/"*.gni 2>/dev/null | head -3
grep -rn -A6 "skia_ports_freetype_sources = " "$S/flutter/third_party/skia/gn/skia.gni" 2>/dev/null | head -12
echo
echo "  wer haengt an typeface_freetype?"
grep -n "typeface_freetype" "$S/flutter/skia/BUILD.gn"

echo
echo "=== abseil: wo steht ABSL_LOW_LEVEL_ALLOC_MISSING?"
grep -rn "ABSL_LOW_LEVEL_ALLOC_MISSING" "$S/third_party/abseil-cpp/absl/base/config.h" \
  "$S/third_party/abseil-cpp/absl/base/internal/low_level_alloc.h" 2>/dev/null | head -10
echo
echo "  Groesse des gebauten Objekts:"
O="$OUT/obj/third_party/abseil-cpp/absl/base/internal/malloc_internal.low_level_alloc.o"
ls -l "$O" 2>/dev/null | awk '{print "  " $5 " Bytes"}'
"$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm" --defined-only "$O" 2>/dev/null | wc -l \
  | sed 's/^/  definierte Symbole: /'
