#!/usr/bin/env bash
set -u
A="$HOME/engine/flutter/engine/src/third_party/abseil-cpp/absl/base/internal"

echo "=== low_level_alloc.cc: Groesse"
wc -l "$A/low_level_alloc.cc"

echo
echo "=== mmap/munmap/sbrk-Stellen"
grep -n "mmap\|munmap\|sbrk\|MAP_\|PROT_\|ABSL_HAVE_MMAP\|_WIN32\|VirtualAlloc" \
  "$A/low_level_alloc.cc"

echo
echo "=== Bedingung in low_level_alloc.h"
sed -n '30,50p' "$A/low_level_alloc.h"

echo
echo "=== wo wird ABSL_HAVE_MMAP gesetzt?"
grep -n -B6 -A4 "define ABSL_HAVE_MMAP" \
  "$HOME/engine/flutter/engine/src/third_party/abseil-cpp/absl/base/config.h"
