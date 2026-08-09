#!/usr/bin/env bash
# Prüft, ob der Engine-Checkout die Teile enthält, die für Meilenstein 2 nötig sind.
set -u

SRC="${SRC:-$HOME/engine/flutter/engine/src}"

echo "Checkout: $SRC"
echo

# Achtung: Die DEPS legen die Abhängigkeiten unter engine/src/flutter/third_party
# ab, nicht unter engine/src/third_party. Letzteres existiert zwar, enthält aber
# nur einen kleinen Teil.
for d in \
  flutter/third_party/dart \
  flutter/third_party/skia \
  flutter/third_party/icu \
  flutter/third_party/libcxx \
  flutter/third_party/gn/gn \
  flutter/buildtools \
  build/config/BUILDCONFIG.gn \
  build/toolchain/qnx/BUILD.gn
do
  if [ -e "$SRC/$d" ]; then
    echo "  ok     $d"
  else
    echo "  FEHLT  $d"
  fi
done

echo
echo "Dart-Runtime-Dateien (fuer den OS-Port relevant):"
for f in \
  flutter/third_party/dart/runtime/vm/os_linux.cc \
  flutter/third_party/dart/runtime/vm/virtual_memory_posix.cc \
  flutter/third_party/dart/runtime/vm/os_thread_linux.cc \
  flutter/third_party/skia/BUILD.gn
do
  if [ -e "$SRC/$f" ]; then
    echo "  ok     $f"
  else
    echo "  FEHLT  $f"
  fi
done
