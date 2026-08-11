#!/usr/bin/env bash
# Baut die ganze Kette in der einzig richtigen Reihenfolge neu.
#
# Warum es dieses Skript gibt: Der Snapshot-Hash ist ein MD5 ueber 15
# Dart-Quelldateien (dart/tools/make_version.py), darunter dart.cc,
# app_snapshot.cc und image_snapshot.cc. Wer eine davon anfasst - und sei es
# nur, um eine Debug-Marke zu entfernen -, macht jeden vorhandenen Snapshot
# ungueltig. gen_snapshot, Engine und Snapshot muessen dann aus demselben Stand
# kommen, sonst endet der naechste Lauf in
#
#   Wrong full snapshot version, expected '...' found '...'
#
# Die Reihenfolge ist deshalb nicht beliebig:
#   1. gen_snapshot  - traegt den neuen Hash
#   2. Engine        - traegt denselben Hash, erwartet ihn im Snapshot
#   3. Snapshot      - mit dem gen_snapshot aus Schritt 1 erzeugt
#   4. NRO           - linkt Engine und Snapshot zusammen
set -u

REPO=/mnt/e/flutter-libnx
SRC="${SRC:-$HOME/engine/flutter/engine/src}"
OUT="${OUT:-out/horizon_release_arm64}"
EXAMPLE="${1:-ui_app}"
GEN_SNAPSHOT="$SRC/$OUT/clang_x64/gen_snapshot_product"
GENERATED="$REPO/examples/$EXAMPLE/generated"
LOG_DIR="$REPO/build-logs"

mkdir -p "$LOG_DIR"

echo "==> 1/4 gen_snapshot"
if ! bash "$REPO/scripts/build-horizon.sh" clang_x64/gen_snapshot_product \
     > "$LOG_DIR/rebuild-gen-snapshot.log" 2>&1; then
  echo "FEHLGESCHLAGEN - siehe $LOG_DIR/rebuild-gen-snapshot.log"
  grep -E "error:" "$LOG_DIR/rebuild-gen-snapshot.log" | head -10
  exit 1
fi
tail -1 "$LOG_DIR/rebuild-gen-snapshot.log"

echo "==> 2/4 Engine"
if ! bash "$REPO/scripts/build-horizon.sh" \
     flutter/shell/platform/embedder:flutter_engine_static \
     > "$LOG_DIR/rebuild-engine.log" 2>&1; then
  echo "FEHLGESCHLAGEN - siehe $LOG_DIR/rebuild-engine.log"
  grep -E "error:" "$LOG_DIR/rebuild-engine.log" | head -10
  exit 1
fi
tail -1 "$LOG_DIR/rebuild-engine.log"

echo "==> 3/4 Snapshot aus $EXAMPLE/generated/app.dill"
if [ ! -x "$GEN_SNAPSHOT" ]; then
  echo "FEHLER: $GEN_SNAPSHOT fehlt"
  exit 1
fi
if [ ! -f "$GENERATED/app.dill" ]; then
  echo "FEHLER: $GENERATED/app.dill fehlt - erst scripts/build-ui-app.ps1 laufen lassen"
  exit 1
fi
# In eine Nebendatei schreiben und erst danach umbenennen: Bricht gen_snapshot
# ab, bleibt der alte Snapshot stehen, statt dass eine leere .s zurueckbleibt
# und der naechste Build stillschweigend Unsinn linkt.
if ! "$GEN_SNAPSHOT" --snapshot_kind=app-aot-assembly \
     --assembly="$GENERATED/app_aot_new.s" "$GENERATED/app.dill"; then
  echo "FEHLGESCHLAGEN: gen_snapshot"
  rm -f "$GENERATED/app_aot_new.s"
  exit 1
fi
if [ ! -s "$GENERATED/app_aot_new.s" ]; then
  echo "FEHLER: erzeugte Assembly ist leer"
  rm -f "$GENERATED/app_aot_new.s"
  exit 1
fi
mv -f "$GENERATED/app_aot_new.s" "$GENERATED/app_aot.s"
echo "    $(wc -l < "$GENERATED/app_aot.s") Zeilen Assembly"

echo "==> 4/4 NRO"
bash "$REPO/scripts/relink-example.sh" "$EXAMPLE"
