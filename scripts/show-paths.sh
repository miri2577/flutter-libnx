#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src/flutter/fml"
echo "=== paths_qnx.cc"
cat "$S/platform/qnx/paths_qnx.cc"
echo
echo "=== paths_posix.cc: definierte Funktionen"
grep -n "^[a-zA-Z].*(" "$S/platform/posix/paths_posix.cc"
echo
echo "=== BUILD.gn, is_horizon-Block"
sed -n '244,275p' "$S/BUILD.gn"
echo
echo "=== Wer ruft GetCachesDirectory?"
grep -rn "GetCachesDirectory" "$HOME/engine/flutter/engine/src/flutter" --include=*.cc --include=*.h | grep -v unittests
