#!/usr/bin/env bash
# Klaert, womit sich ein poll-basierter Eventhandler auf Horizon wecken laesst.
D="$HOME/devkitpro"
NM="$D/devkitA64/bin/aarch64-none-elf-nm"

echo "=== Header"
for h in "$D/libnx/include/poll.h" \
         "$D/libnx/include/sys/socket.h" \
         "$D/devkitA64/aarch64-none-elf/include/unistd.h"; do
  if [ -e "$h" ]; then echo "  ok    $h"; else echo "  FEHLT $h"; fi
done

echo
echo "=== Deklarationen"
grep -hn "int poll *(" "$D/libnx/include/poll.h" 2>/dev/null | head -2
grep -hn "socketpair" "$D/libnx/include/sys/socket.h" 2>/dev/null | head -2
grep -hn "int[ 	]*pipe" "$D/devkitA64/aarch64-none-elf/include/unistd.h" 2>/dev/null | head -3

echo
echo "=== In libnx tatsaechlich definierte Symbole"
"$NM" -g --defined-only "$D/libnx/lib/libnx.a" 2>/dev/null \
  | grep -E " T (poll|select|socketpair|pipe|pipe2)$" | sort -u
