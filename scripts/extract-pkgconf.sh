#!/usr/bin/env bash
# Holt pkg-config (pkgconf) ohne root nach ~/bin.
#
# Der Engine-Build braucht das Programm für die Host-Toolchain; die zugehörigen
# Bibliotheken kommen aus dem von gclient geladenen Debian-Sysroot, nicht vom
# System. Es fehlt also nur die ausführbare Datei.
#
# Quelle ist das devkitPro-Image, das ohnehin schon lokal liegt.
set -euo pipefail

TAR="/mnt/e/pkgconf.tar"
BIN="$HOME/bin"

mkdir -p "$BIN"
rm -rf /tmp/pkgx
mkdir -p /tmp/pkgx
tar -xf "$TAR" -C /tmp/pkgx

LIB="$HOME/lib"
mkdir -p "$LIB"

# Kein Symlink von pkg-config auf pkgconf: Das Wrapper-Skript unten wuerde dem
# Symlink folgen und pkgconf selbst ueberschreiben.
rm -f "$BIN/pkg-config"
cp -a /tmp/pkgx/usr/bin/pkgconf "$BIN/"
cp -a /tmp/pkgx/usr/lib/x86_64-linux-gnu/libpkgconf.so.3* "$LIB/" 2>/dev/null || true

# pkgconf bringt eine eigene Bibliothek mit, die nicht im Systempfad liegt.
# Ein Wrapper setzt LD_LIBRARY_PATH, damit der Aufrufer davon nichts wissen muss.
cat > "$BIN/pkg-config" <<EOF
#!/bin/sh
LD_LIBRARY_PATH="$LIB:\${LD_LIBRARY_PATH:-}" exec "$BIN/pkgconf" "\$@"
EOF
chmod +x "$BIN/pkg-config"

echo "installiert nach $BIN"
"$BIN/pkg-config" --version
