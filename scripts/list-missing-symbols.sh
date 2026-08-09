#!/usr/bin/env bash
# Tatsaechlich fehlende Symbole: undefinierte minus die, die in irgendeiner
# Objektdatei desselben Ziels definiert sind.
#
# Die erste Fassung dieses Skripts hat nur die undefinierten gezaehlt und die
# Zahl damit deutlich ueberschaetzt - Querverweise zwischen Objekten sind der
# Normalfall, kein Fehlen.
set -u

NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
OBJ="$HOME/engine/flutter/engine/src/out/horizon_release_arm64/obj/flutter/third_party/dart/runtime/vm"

cd "$OBJ" || exit 1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$NM" --undefined-only -C libdart_vm_aotruntime_product.*.o 2>/dev/null \
  | sed 's/^ *U //' | sort -u > "$TMP/undef"

"$NM" --defined-only -C libdart_vm_aotruntime_product.*.o 2>/dev/null \
  | sed 's/^[0-9a-f]* [A-Za-z] //' | sort -u > "$TMP/def"

comm -23 "$TMP/undef" "$TMP/def" > "$TMP/missing"

echo "undefiniert:        $(wc -l < "$TMP/undef")"
echo "davon anderswo def: $(comm -12 "$TMP/undef" "$TMP/def" | wc -l)"
echo "tatsaechlich offen: $(wc -l < "$TMP/missing")"
echo

for prefix in "dart::OS::" "dart::OSThread::" "dart::VirtualMemory::" "dart::CpuInfo" "dart::NativeSymbolResolver"; do
  echo "########## $prefix  ($(grep -c "$prefix" "$TMP/missing"))"
  grep "$prefix" "$TMP/missing"
  echo
done

echo "########## sonstige offene dart::-Symbole"
grep "^dart::" "$TMP/missing" \
  | grep -vE "dart::(OS|OSThread|VirtualMemory|CpuInfo|NativeSymbolResolver)" | head -25
