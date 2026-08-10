#!/usr/bin/env bash
set -u
B="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"

for f in socket_base_posix.cc console_posix.cc namespace_linux.cc \
         crypto_linux.cc security_context_linux.cc; do
  echo "=== $f: Kopf"
  sed -n '1,30p' "$B/$f"
  echo "--- Abschluss"
  tail -3 "$B/$f"
  echo
done

echo "=== socket_base_posix.cc: ListInterfaces"
grep -n -A45 "SocketBase::ListInterfaces" "$B/socket_base_posix.cc"
