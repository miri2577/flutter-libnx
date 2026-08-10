#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
D="$HOME/devkitpro"
echo "=== socket_base_linux.h (vollstaendig)"
cat "$R/socket_base_linux.h"
echo
echo "=== RawAddr in socket_base.h"
grep -n -A16 "struct RawAddr" "$R/socket_base.h" | head -22
echo
echo "=== sockaddr_un / AF_UNIX in libnx"
grep -rn "sockaddr_un\|AF_UNIX" "$D/libnx/include/sys/socket.h" "$D/libnx/include/switch/runtime/devices/socket.h" 2>/dev/null | head -5
ls "$D/libnx/include/sys/un.h" 2>/dev/null || echo "  kein sys/un.h"
