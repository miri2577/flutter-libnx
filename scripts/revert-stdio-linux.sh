#!/usr/bin/env bash
# Nimmt die HORIZON-Bedingung aus stdio_linux.cc wieder heraus.
#
# Die Datei war zunaechst zur Wiederverwendung vorgesehen, besteht aber fast
# vollstaendig aus Terminalsteuerung ueber termios. Dafuer gibt es jetzt
# stdio_horizon.cc; blieben beide aktiv, gaebe es doppelte Symbole.
#
# Nebenbefund: third_party/dart ist ein eigenes Git-Repository innerhalb des
# Checkouts. Rueckrollen von Dart-Dateien braucht deshalb -C auf dieses
# Verzeichnis, nicht auf den flutter-Baum.
set -eu

D="$HOME/engine/flutter/engine/src/flutter/third_party/dart"
F="$D/runtime/bin/stdio_linux.cc"

git -C "$D" checkout -- runtime/bin/stdio_linux.cc
echo "zurueckgesetzt."
echo "HORIZON-Treffer danach: $(grep -c 'DART_HOST_OS_HORIZON' "$F" || true)"
