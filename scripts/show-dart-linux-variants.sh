#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime"
echo "########## platform/utils_linux.h"
cat "$R/platform/utils_linux.h"
echo
echo "########## vm/os_thread_linux.h"
cat "$R/vm/os_thread_linux.h"
