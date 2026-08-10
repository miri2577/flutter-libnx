#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src"

echo "=== Wer bindet absl/synchronization ein?"
grep -rl "absl/synchronization/mutex.h" "$S/flutter" \
  --include=*.cc --include=*.h 2>/dev/null | head -10

echo
echo "=== Wer bindet absl/debugging (Stacktrace) ein?"
grep -rl "absl/debugging" "$S/flutter" --include=*.cc --include=*.h 2>/dev/null | head -10

echo
echo "=== abseil-Ziele, die die Engine als deps fuehrt"
grep -rn "abseil" "$S/flutter/shell/platform/embedder/BUILD.gn" \
  "$S/flutter/BUILD.gn" 2>/dev/null | head -10

echo
echo "=== welche abseil-Objekte wurden ueberhaupt gebaut"
find "$S/out/horizon_release_arm64/obj/third_party/abseil-cpp" -name "*.o" 2>/dev/null \
  | sed "s|.*/||" | sort | head -40
echo "  insgesamt: $(find "$S/out/horizon_release_arm64/obj/third_party/abseil-cpp" -name '*.o' 2>/dev/null | wc -l)"

echo
echo "=== per_thread_sem / create_thread_identity gebaut?"
find "$S/out/horizon_release_arm64/obj/third_party/abseil-cpp" \
  \( -name "*per_thread_sem*" -o -name "*thread_identity*" -o -name "*waiter*" \) 2>/dev/null
