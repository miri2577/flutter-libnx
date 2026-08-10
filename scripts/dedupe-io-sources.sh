#!/usr/bin/env bash
# Entfernt doppelte horizon-Eintraege aus io_impl_sources.gni.
set -eu

F="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin/io_impl_sources.gni"

echo "vorher:"
grep -n "horizon" "$F" || true

awk '!(/horizon/ && seen[$0]++)' "$F" > "$F.tmp"
mv "$F.tmp" "$F"

echo
echo "nachher:"
grep -n "horizon" "$F" || true
