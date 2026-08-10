#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
for f in socket_base.h socket.h eventhandler.h; do
  echo "########## $f"
  grep -n -A2 "DART_HOST_OS_LINUX" "$f" 2>/dev/null || grep -n -B2 -A8 "DART_HOST_OS_LINUX" "$R/$f" | head -14
  echo
done
echo "########## Groessen der Linux-Header"
wc -l "$R"/socket_base_linux.h "$R"/socket_linux.h "$R"/eventhandler_linux.h 2>/dev/null
echo
echo "########## eventhandler_linux.h – epoll-Nutzung"
grep -c "epoll" "$R/eventhandler_linux.h"
