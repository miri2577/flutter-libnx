#!/usr/bin/env bash
R="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
for f in thread_linux.cc fdutils_linux.cc utils_linux.cc crypto_linux.cc \
         socket_base_linux.cc file_linux.cc; do
  echo "########## $f"
  grep -n "DART_HOST_OS_LINUX" "$R/$f" | head -3
  echo "  -- benutzte Linux-Spezialitaeten:"
  grep -on "epoll\|inotify\|/proc/\|/dev/urandom\|getrandom\|sendfile\|statfs\|prctl\|eventfd\|timerfd\|SOCK_CLOEXEC\|O_CLOEXEC\|accept4\|pipe2" "$R/$f" \
    | cut -d: -f2 | sort -u | tr '\n' ' '
  echo
done
