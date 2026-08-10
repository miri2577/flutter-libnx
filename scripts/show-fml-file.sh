#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src/flutter/fml"

echo "=== file_posix.cc: Stellen mit *at / opendir"
grep -n "openat\|mkdirat\|unlinkat\|fdopendir\|dirfd\|opendir\|::open(" "$S/platform/posix/file_posix.cc"

echo
echo "=== OpenDirectory / VisitFiles im Original"
sed -n '95,130p;225,265p' "$S/platform/posix/file_posix.cc"

echo
echo "=== fml/paths.h"
grep -n "GetCachesDirectory\|GetExecutable\|std::string\|std::pair" "$S/paths.h"

echo
echo "=== Vorhandene paths-Implementierungen"
ls "$S/platform"/*/paths_*.cc 2>/dev/null

echo
echo "=== paths_linux.cc"
cat "$S/platform/linux/paths_linux.cc" 2>/dev/null

echo
echo "=== BUILD.gn: paths/file Quellen"
grep -n "paths_\|file_posix\|is_horizon\|platform/horizon" "$S/BUILD.gn"
