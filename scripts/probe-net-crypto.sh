#!/usr/bin/env bash
set -u
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
LIBDIR="$HOME/devkitpro/devkitA64/aarch64-none-elf/lib"
NXLIB="$HOME/devkitpro/libnx/lib"
D="$HOME/devkitpro/devkitA64/aarch64-none-elf/include"
NXINC="$HOME/devkitpro/libnx/include"

check() {
  local s="$1"
  local found=""
  for lib in "$LIBDIR"/libc.a "$LIBDIR"/libsysbase.a "$NXLIB"/libnx.a; do
    [ -f "$lib" ] || continue
    "$NM" --defined-only "$lib" 2>/dev/null | grep -qE "^[0-9a-f]+ +[TW] +$s\$" \
      && found="$found $(basename "$lib" .a)"
  done
  if [ -n "$found" ]; then
    printf "  ja   %-22s <-%s\n" "$s" "$found"
  else
    printf "  NEIN %-22s\n" "$s"
  fi
}

echo "=== Netzwerk (fuer socket_base_posix.cc)"
for s in getifaddrs freeifaddrs if_nametoindex gai_strerror inet_ntop \
         getaddrinfo freeaddrinfo getnameinfo socket bind listen accept \
         connect setsockopt getsockopt shutdown recvmsg sendmsg; do
  check "$s"
done

echo
echo "=== Zufall / Crypto"
for s in getentropy _getentropy_r getrandom csrngGetRandomBytes randomGet; do
  check "$s"
done

echo
echo "=== Datei-/Prozess-Luecken aus der Symbolliste"
for s in fstatat pread readlinkat symlinkat utimensat pipe pthread_sigmask \
         open64 fchdir dup2 getcwd chdir; do
  check "$s"
done

echo
echo "=== libnx-Zufallsquelle: Header"
grep -rn "csrngGetRandomBytes\|randomGet" "$NXINC" 2>/dev/null | head -5

echo
echo "=== bin/ifaddrs.h im Dart-Baum"
B="$HOME/engine/flutter/engine/src/flutter/third_party/dart/runtime/bin"
sed -n '1,40p' "$B/ifaddrs.h" 2>/dev/null
