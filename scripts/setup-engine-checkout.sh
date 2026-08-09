#!/usr/bin/env bash
# Legt den gclient-Checkout der Flutter Engine an (Meilenstein 2).
#
# Der Checkout landet bewusst im WSL-Dateisystem (~/engine) und nicht unter
# /mnt/e: Er enthält Symlinks und braucht POSIX-Rechte, und der Zugriff über
# 9p wäre für ~100k Dateien quälend langsam. Physisch liegt er trotzdem auf E:,
# weil die ext4-VHDX der Distribution dort liegt.
set -euo pipefail

ENGINE_DIR="${ENGINE_DIR:-$HOME/engine}"
DEPOT_TOOLS="${DEPOT_TOOLS:-$HOME/depot_tools}"
# Gepinnt auf Flutter 3.41.6 – siehe README.md.
FLUTTER_REV="db50e20168db8fee486b9abf32fc912de3bc5b6a"

echo "==> depot_tools"
if [ ! -d "$DEPOT_TOOLS/.git" ]; then
  git clone --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
else
  echo "    bereits vorhanden"
fi
export PATH="$DEPOT_TOOLS:$PATH"
# depot_tools aktualisiert sich sonst bei jedem Aufruf selbst und bricht damit
# die Reproduzierbarkeit.
export DEPOT_TOOLS_UPDATE=0

echo "==> Arbeitsverzeichnis $ENGINE_DIR"
mkdir -p "$ENGINE_DIR"
cd "$ENGINE_DIR"

# download_android_deps spart mehrere GB und einige Minuten; wir bauen kein
# Android. Dasselbe gilt für das Emscripten-SDK.
cat > .gclient <<'EOF'
solutions = [
  {
    "managed": False,
    "name": "flutter",
    "url": "https://github.com/flutter/flutter.git",
    "deps_file": "DEPS",
    "custom_vars": {
      "download_android_deps": False,
      "download_emsdk": False,
    },
  },
]
EOF

echo "==> flutter/flutter auschecken ($FLUTTER_REV)"
if [ ! -d flutter/.git ]; then
  git clone --filter=blob:none https://github.com/flutter/flutter.git flutter
fi
cd flutter
git fetch --filter=blob:none origin "$FLUTTER_REV" 2>/dev/null || git fetch origin
git checkout -q "$FLUTTER_REV"
echo "    HEAD: $(git rev-parse --short HEAD) ($(git describe --tags 2>/dev/null || echo 'kein Tag'))"
cd ..

echo "==> gclient sync (das dauert; bei 100 Mbit/s grob 30-90 Minuten)"
gclient sync -D --no-history --shallow

echo "==> fertig"
du -sh "$ENGINE_DIR"
