#!/usr/bin/env bash
# Baut sqlite3 (Amalgamation, third_party/sqlite3) als statische Bibliothek
# fuer Horizon und erzeugt die Symboltabelle fuer die FFI-Aufloesung.
#
# Warum selbst gebaut: devkitPro fuehrt kein sqlite-Portlib (dkp-pacman:
# "target not found"), und die Amalgamation ist genau fuer diesen Fall
# gemacht - eine Uebersetzungseinheit, Verhalten komplett ueber Defines.
#
# Die Defines, jede mit Grund:
#   SQLITE_THREADSAFE=1        drift laeuft in einem Hintergrund-Isolate,
#                              also auf einem anderen Thread
#   SQLITE_OMIT_LOAD_EXTENSION kein dlopen auf Horizon
#   SQLITE_OMIT_WAL            WAL braucht Shared Memory + Dateisperren
#   SQLITE_MAX_MMAP_SIZE=0     kein mmap; kompiliert die mmap-Pfade aus
#   SQLITE_TEMP_STORE=3        Tempdaten im Speicher statt Dateien auf FAT
#   SQLITE_DISABLE_DIRSYNC     devoptab kann Verzeichnisse nicht fsyncen
#   SQLITE_DEFAULT_MEMSTATUS=0 erspart eine globale Sperre je Allokation
#   HAVE_USLEEP=1              newlib hat usleep; sonst schlaeft Busy-Retry
#                              sekundenweise
#   SQLITE_ENABLE_FTS5         Volltextsuche - rezkonv scheiterte an
#                              "no such module: fts5"; drift-Apps nutzen
#                              das regelmaessig
#   SQLITE_ENABLE_MATH_FUNCTIONS  sin/cos/pow in SQL; von drift empfohlen
#   SQLITE_ENABLE_RTREE        Bereichsindizes; klein und verbreitet
#
# Dateisperren: bewusst NICHT wegkonfiguriert, sondern zur Laufzeit geloest -
# der Embedder macht "unix-none" (keine Sperren) zum Standard-VFS, sobald die
# Bibliothek zum ersten Mal angefordert wird. Auf einer Konsole greift kein
# zweiter Prozess auf die Datenbank zu.
set -eu

REPO=/mnt/e/flutter-libnx
SRC="$REPO/third_party/sqlite3"
DEVKITA64="$HOME/devkitpro/devkitA64"
CC="$DEVKITA64/bin/aarch64-none-elf-gcc"
AR="$DEVKITA64/bin/aarch64-none-elf-gcc-ar"
NM="$DEVKITA64/bin/aarch64-none-elf-nm"

cd "$SRC"

echo "==> sqlite3.c uebersetzen"
"$CC" \
  -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC \
  -O2 -g -ffunction-sections -fdata-sections \
  -D__SWITCH__ \
  -I"$HOME/devkitpro/libnx/include" \
  -DSQLITE_THREADSAFE=1 \
  -DSQLITE_OMIT_LOAD_EXTENSION \
  -DSQLITE_OMIT_WAL \
  -DSQLITE_MAX_MMAP_SIZE=0 \
  -DSQLITE_TEMP_STORE=3 \
  -DSQLITE_DISABLE_DIRSYNC \
  -DSQLITE_DEFAULT_MEMSTATUS=0 \
  -DHAVE_USLEEP=1 \
  -DSQLITE_ENABLE_FTS5 \
  -DSQLITE_ENABLE_MATH_FUNCTIONS \
  -DSQLITE_ENABLE_RTREE \
  -c sqlite3.c -o sqlite3.o

"$AR" rcs libsqlite3.a sqlite3.o
echo "    $(du -h libsqlite3.a | cut -f1) libsqlite3.a"

# Symboltabelle: alle exportierten sqlite3_*-Symbole. Die Tabelle dient
# doppelt - als Nachschlagewerk fuer die FFI-Aufloesung und als Anker, der
# den Linker zwingt, die Objekte ueberhaupt einzubinden (nichts sonst im
# Programm referenziert sqlite3_*).
#
# Nicht nur T (Funktionen): sqlite3_temp_directory und sqlite3_data_directory
# sind globale VARIABLEN (B/D) - package:sqlite3 setzt sie per FFI. Der
# erste Lauf ohne sie scheiterte genau daran. Die Als-Funktion-Deklaration
# in static_libraries_horizon.cpp ist dafuer unerheblich: Es zaehlt nur die
# Adresse, und der Linker kennt keine Typen.
echo "==> Symboltabelle erzeugen"
{
  echo "// Von scripts/build-sqlite3.sh erzeugt - nicht von Hand pflegen."
  echo "// Format: SQLITE3_SYMBOL(name) je exportiertem Symbol."
  "$NM" --defined-only libsqlite3.a \
    | awk '$2 ~ /^[TDBRG]$/ && $3 ~ /^sqlite3_/ { print "SQLITE3_SYMBOL(" $3 ")" }' \
    | sort -u
} > sqlite3_symbols.inc
echo "    $(grep -c SQLITE3_SYMBOL sqlite3_symbols.inc) Symbole"
