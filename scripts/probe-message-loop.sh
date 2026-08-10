#!/usr/bin/env bash
set -u
S="$HOME/engine/flutter/engine/src/flutter/fml"
OUT="$HOME/engine/flutter/engine/src/out/horizon_release_arm64"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"

echo "=== MessageLoopImpl::Create"
grep -n -A25 "MessageLoopImpl::Create" "$S/message_loop_impl.cc"

echo
echo "=== message_loop-Quellen in BUILD.gn"
grep -n "message_loop" "$S/BUILD.gn"

echo
echo "=== welche message_loop-Objekte wurden gebaut"
ls "$OUT/obj/flutter/fml/platform"/*/*message_loop* 2>/dev/null
ls "$OUT/obj/flutter/fml"/*message_loop* 2>/dev/null

echo
echo "=== timerfd/epoll im gelinkten Programm?"
E="/mnt/e/flutter-libnx/examples/engine_link_test/engine_link_test.elf"
for s in timerfd_create epoll_create1 epoll_wait eventfd; do
  if "$NM" --undefined-only "$E" 2>/dev/null | grep -q " U $s\$"; then
    echo "  UNDEFINIERT $s"
  elif "$NM" "$E" 2>/dev/null | grep -qE " [TW] $s\$"; then
    echo "  definiert   $s"
  else
    echo "  nicht referenziert  $s"
  fi
done
