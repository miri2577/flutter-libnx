#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src"
A="$S/third_party/abseil-cpp/absl"

echo "=== synchronization/BUILD.gn: Quellen und Bedingungen"
grep -n "create_thread_identity\|per_thread_sem\|low_level_alloc\|sources\|deps\|if (" \
  "$A/synchronization/BUILD.gn" 2>/dev/null | head -30

echo
echo "=== create_thread_identity.cc: Umgang mit fehlendem LowLevelAlloc"
grep -n "ABSL_LOW_LEVEL_ALLOC_MISSING" -A8 -B4 \
  "$A/base/internal/thread_identity.cc" \
  "$A/synchronization/internal/create_thread_identity.cc" 2>/dev/null | head -40

echo
echo "=== wer fuehrt absl/synchronization als dep?"
grep -rn "absl/synchronization" "$S/flutter/third_party/re2/BUILD.gn" 2>/dev/null | head
grep -rn "synchronization" "$S/third_party/abseil-cpp/BUILD.gn" 2>/dev/null | head -5

echo
echo "=== ninja kennt das Ziel?"
ls "$S/out/horizon_release_arm64/obj/third_party/abseil-cpp/absl/synchronization/" 2>/dev/null | head -20
