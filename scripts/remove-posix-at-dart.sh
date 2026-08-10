#!/usr/bin/env bash
# Entfernt posix_at_horizon.cc aus dem Dart-Baum. Die *at-Funktionen liegen
# jetzt im Embedder (posix_compat_horizon.cpp) und beherrschen dort echte
# Verzeichnis-Handles. Zwei Definitionen waeren ein Linkfehler.
set -eu
BIN="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
GNI="$BIN/io_impl_sources.gni"

echo "vorher:"
grep -n "posix_at_horizon" "$GNI" || echo "  (kein Eintrag)"

if grep -q "posix_at_horizon" "$GNI"; then
  grep -v '"posix_at_horizon.cc",' "$GNI" > "$GNI.tmp"
  mv "$GNI.tmp" "$GNI"
  echo "  Eintrag entfernt"
fi

if [ -f "$BIN/posix_at_horizon.cc" ]; then
  rm "$BIN/posix_at_horizon.cc"
  echo "  Datei entfernt"
fi

echo "nachher:"
grep -n "horizon" "$GNI"
