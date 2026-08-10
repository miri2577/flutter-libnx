#!/usr/bin/env bash
set -u
F="$HOME/engine/flutter/engine/src/third_party/abseil-cpp/absl/base/internal/low_level_alloc.cc"
echo "=== 25-70"
sed -n '25,70p' "$F"
echo
echo "=== 425-455"
sed -n '425,455p' "$F"
echo
echo "=== 565-620"
sed -n '565,620p' "$F"
