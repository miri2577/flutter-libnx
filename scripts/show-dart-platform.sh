#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime"
for f in platform/utils.h platform/threads.h platform/synchronization.h vm/os_thread.h; do
  echo "########## $f"
  grep -n -B2 -A14 "DART_HOST_OS_LINUX\|DART_HOST_OS_ANDROID" "$R/$f" | head -30
  echo
done
echo "########## vorhandene Linux-Varianten"
ls "$R/platform/" | grep -E "linux" | head -20
ls "$R/vm/" | grep -E "^os_thread_linux" | head
