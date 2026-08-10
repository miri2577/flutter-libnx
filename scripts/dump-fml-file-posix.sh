#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src/flutter/fml"
echo "=== file_posix.cc ($(wc -l < "$S/platform/posix/file_posix.cc") Zeilen)"
cat -n "$S/platform/posix/file_posix.cc"
echo
echo "=== mapping_horizon.cc: fd-Nutzung"
grep -n "fstat\|::open\|lseek" "$S/platform/horizon/mapping_horizon.cc"
