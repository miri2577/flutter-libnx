#!/usr/bin/env bash
# Loest Absturzadressen aus dem Crash-Handler-Bericht gegen ui_app.elf auf.
# Aufruf: resolve-crash.sh <Handler-Laufzeitadresse> <PC> <LR> [weitere...]
# Der erste Wert ist der "Bezugspunkt" aus dem Bericht
# (__libnx_exception_handler); daraus ergibt sich die NRO-Basis.
set -eu

ELF=/mnt/e/flutter-libnx/examples/ui_app/ui_app.elf
TOOLS=$(dirname "$(find "$HOME/devkitpro/devkitA64/bin" -name 'aarch64-none-elf-nm' | head -1)")
NM="$TOOLS/aarch64-none-elf-nm"
A2L="$TOOLS/aarch64-none-elf-addr2line"

HANDLER_RT=$1
shift

HANDLER_ELF=0x$("$NM" "$ELF" | awk '$3 == "__libnx_exception_handler" {print $1}')
BASE=$(( HANDLER_RT - HANDLER_ELF ))
printf 'Handler in ELF: %s, NRO-Basis: 0x%x\n' "$HANDLER_ELF" "$BASE"

for addr in "$@"; do
  rel=$(( addr - BASE ))
  printf '%s -> ELF 0x%x: ' "$addr" "$rel"
  "$A2L" -e "$ELF" -f -C -i "$(printf '0x%x' $rel)" | tr '\n' ' '
  echo
done
