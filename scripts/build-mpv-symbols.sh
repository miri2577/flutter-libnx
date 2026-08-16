#!/usr/bin/env bash
# Erzeugt die Symboltabelle fuer das statisch gelinkte libmpv
# (devkitPro-Portlib switch-libmpv) - Gegenstueck zu build-sqlite3.sh.
#
# media_kit spricht libmpv ueber dart:ffi an (DynamicLibrary.open +
# Symbolaufloesung); die Tabelle bedient den dl-Haken des Embedders.
# Wie bei sqlite3 zaehlen auch Datensymbole (D/B/R/G), nicht nur
# Funktionen, und die Tabelle ist zugleich der Anker, der den Linker
# zwingt, libmpv ueberhaupt in die NRO aufzunehmen.
set -eu

REPO=/mnt/e/flutter-libnx
OUT="$REPO/third_party/mpv"
LIB="$HOME/devkitpro/portlibs/switch/lib/libmpv.a"
NM="$HOME/devkitpro/devkitA64/bin/aarch64-none-elf-nm"

mkdir -p "$OUT"

echo "==> Symboltabelle aus $LIB"
{
  echo "// Von scripts/build-mpv-symbols.sh erzeugt - nicht von Hand pflegen."
  echo "// Format: MPV_SYMBOL(name) je exportiertem mpv_*-Symbol."
  "$NM" --defined-only "$LIB" \
    | awk '$2 ~ /^[TDBRG]$/ && $3 ~ /^mpv_/ { print "MPV_SYMBOL(" $3 ")" }' \
    | sort -u
} > "$OUT/mpv_symbols.inc"
echo "    $(grep -c MPV_SYMBOL "$OUT/mpv_symbols.inc") Symbole"
