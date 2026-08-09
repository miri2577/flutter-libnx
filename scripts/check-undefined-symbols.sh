#!/usr/bin/env bash
# Zaehlt die undefinierten Symbole der Dart-VM, die aus der fehlenden
# Horizon-Portierung stammen.
#
# Uebersetzen heisst nicht vollstaendig: Dart uebersetzt alle os_*.cc, aber
# jede verbirgt ihren Inhalt hinter #if defined(DART_HOST_OS_...). Fuer Horizon
# bleiben sie leer - der Linker wird es merken.
set -u

NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
OBJ="$HOME/engine/flutter/engine/src/out/horizon_release_arm64/obj/flutter/third_party/dart/runtime/vm"

cd "$OBJ" || exit 1

ALL=$("$NM" --undefined-only -C libdart_vm_aotruntime_product.*.o 2>/dev/null \
  | sed 's/^ *U //' | sort -u)

echo "=== Undefinierte Symbole gesamt: $(echo "$ALL" | wc -l)"
echo
for prefix in "dart::OS::" "dart::OSThread::" "dart::VirtualMemory::" "dart::ThreadInterrupter"; do
  count=$(echo "$ALL" | grep -c "$prefix" || true)
  echo "  $prefix  ->  $count"
done

echo
echo "=== Beispiele aus dart::OS:: und dart::VirtualMemory:::"
echo "$ALL" | grep -E "dart::(OS|VirtualMemory)::" | head -18
