#!/usr/bin/env bash
set -u
B="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
echo "=== in process.cc definiert (plattformneutral)"
grep -n "^[a-zA-Z_].*Process::" "$B/process.cc" | sed 's/^/  /'
echo
echo "=== in process_horizon.cc definiert"
grep -n "^[a-zA-Z_].*Process::" "$B/process_horizon.cc" | sed 's/^/  /'
