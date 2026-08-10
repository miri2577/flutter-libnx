#!/usr/bin/env bash
set -u
cd "$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin" || exit 1

for f in socket_base_linux.cc socket_linux.cc sync_socket_linux.cc \
         namespace_linux.cc platform_linux.cc process_linux.cc \
         file_system_watcher_linux.cc security_context_linux.cc \
         crypto_linux.cc file_linux.cc directory_linux.cc \
         eventhandler_linux.cc stdio_linux.cc; do
  if grep -q "DART_HOST_OS_HORIZON" "$f" 2>/dev/null; then
    printf "  schon erweitert  %s\n" "$f"
  else
    printf "  OFFEN            %s\n" "$f"
  fi
done
