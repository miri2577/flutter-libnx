#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src"
OUT="$S/out/horizon_release_arm64"

echo "=== Wer ruft sysconf?"
grep -o "in function \`[^']*'" /tmp/link.log | tail -5
grep -B4 "undefined reference to \`sysconf'" /tmp/link.log | head -12

echo
echo "=== _SC_-Konstanten in newlib"
grep -rn "define _SC_PAGESIZE\|define _SC_PAGE_SIZE\|define _SC_NPROCESSORS_ONLN" \
  "$HOME/devkitpro/devkitA64/aarch64-none-elf/include/sys/unistd.h" 2>/dev/null | head

echo
echo "=== dart_use_fallback_root_certificates in args.gn?"
grep "fallback" "$OUT/args.gn" || echo "  nicht gesetzt"

echo
echo "=== wo wird das Flag ausgewertet?"
grep -rn "dart_use_fallback_root_certificates" \
  "$S/flutter/third_party/dart/runtime/bin/BUILD.gn" \
  "$S/flutter/third_party/dart/runtime/runtime_args.gni" 2>/dev/null | head

echo
echo "=== Kontext der Auswertung"
grep -n -B14 "fallback_root_certificates" \
  "$S/flutter/third_party/dart/runtime/bin/BUILD.gn" 2>/dev/null | head -25

echo
echo "=== nutzt die Engine dieses Target?"
grep -rn "root_certificates\|dart_io_api\|libdart_builtin" \
  "$S/flutter/shell/platform/embedder/BUILD.gn" 2>/dev/null | head -5
