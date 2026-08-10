#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src"
OUT="$S/out/horizon_release_arm64"

echo "=== 1. FreeType: wird typeface_freetype gebaut?"
find "$OUT/obj" -path "*freetype*" -name "*.o" 2>/dev/null | head -5
echo "  gefundene FreeType-Objekte: $(find "$OUT/obj" -path "*freetype*" -name "*.o" 2>/dev/null | wc -l)"
echo
echo "  typeface_freetype in skia/BUILD.gn:"
grep -n -B3 -A12 'optional("typeface_freetype")' "$S/flutter/skia/BUILD.gn" 2>/dev/null | head -25
echo
echo "  liegt third_party/freetype2 vor?"
ls -d "$S/flutter/third_party/freetype2" 2>/dev/null || echo "  nein"
echo
echo "  wer verlangt SkTypeface_FreeType?"
grep -rn "fontmgr_custom_empty\|SkFontMgr_New_Custom_Empty" "$S/flutter/skia/BUILD.gn" | head -5

echo
echo "=== 2. abseil: LowLevelAlloc"
grep -rn "ABSL_LOW_LEVEL_ALLOC_MISSING" \
  "$S/third_party/abseil-cpp/absl/base/config.h" | head -10
echo
echo "  gebaute low_level_alloc-Objekte:"
find "$OUT/obj" -name "*low_level_alloc*" 2>/dev/null | head -3

echo
echo "=== 3. root_certificates"
grep -rn "root_certificates_pem_length" \
  "$S/flutter/third_party/dart/runtime/bin/"*.h 2>/dev/null | head -3
echo "  Quelle im Baum:"
find "$S/flutter/third_party/dart" -name "root_certificates*" 2>/dev/null | head -5
echo "  GN-Ziel:"
grep -rn "root_certificates" "$S/flutter/third_party/dart/runtime/bin/BUILD.gn" 2>/dev/null | head -5
