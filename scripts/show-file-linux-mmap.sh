#!/usr/bin/env bash
F="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin/file_linux.cc"
echo "=== mmap-Nutzung"
grep -n "mmap\|munmap\|PROT_\|MAP_" "$F"
echo
echo "=== File::Map"
grep -n -A30 "void\* File::Map" "$F" | head -40
