#!/usr/bin/env bash
# Erzwingt einen Neulink des Beispiels und fasst das Ergebnis zusammen.
# Das Makefile kennt libflutter_engine.a nicht als Abhaengigkeit, deshalb muss
# die ELF weg, damit ueberhaupt neu gelinkt wird.
set -u
EX="${1:-engine_link_test}"
D="/mnt/e/flutter-libnx/examples/$EX"
# Nicht nach /tmp: Wird die WSL-Instanz zwischen zwei Aufrufen beendet, ist das
# Protokoll weg - und ein leeres Protokoll sieht aus wie "null Fehler".
LOG="/mnt/e/flutter-libnx/build-logs/link-$EX.log"
mkdir -p "$(dirname "$LOG")"

rm -f "$D/$EX.elf"
bash /mnt/e/flutter-libnx/scripts/build-example-wsl.sh "$EX" > "$LOG" 2>&1
BUILD_RC=$?

# Erst prüfen, ob überhaupt gelinkt wurde. Ein leeres Protokoll und ein
# fehlerfreies sehen bei einer reinen Symbolzählung gleich aus.
if [ ! -s "$LOG" ]; then
  echo "FEHLER: Protokoll ist leer - der Build lief gar nicht an."
  exit 1
fi

# Und noch davor: Ein Compilerfehler beendet make, bevor der Linker ueberhaupt
# laeuft. Die Symbolzaehlung meldete dann "0 undefinierte Symbole" - richtig
# gezaehlt und trotzdem irrefuehrend, weil die alte NRO unveraendert liegen
# blieb. Dieselbe Falle wie beim Zaehler, der nur eine Fehlerart kannte.
if [ "$BUILD_RC" -ne 0 ]; then
  echo "FEHLER: make endete mit $BUILD_RC - es wurde nichts gelinkt."
  grep -E "error:|Error [0-9]+" "$LOG" | head -10 | sed 's/^/  /'
  echo "  vollstaendig: $LOG"
  exit 1
fi
if ! grep -q "linking\|built \.\.\.\|Error\|error" "$LOG"; then
  echo "WARNUNG: keine Link- oder Fehlerzeile im Protokoll"
  tail -10 "$LOG" | sed 's/^/  /'
fi

echo "=== undefinierte Symbole"
MISSING="$(dirname "$LOG")/missing-$EX.txt"
grep -o "undefined reference to \`[^']*'" "$LOG" \
  | sed "s/undefined reference to \`//; s/'$//" | sort -u > "$MISSING"
COUNT=$(wc -l < "$MISSING")
echo "  $COUNT"
[ "$COUNT" -gt 0 ] && head -15 "$MISSING" | sed 's/^/    /'

# Nicht jeder Linkfehler ist ein fehlendes Symbol. Eine reine Zaehlung der
# undefinierten Verweise meldete schon einmal "0", waehrend der Link an einer
# doppelten Definition scheiterte.
echo
echo "=== doppelte Definitionen"
DUP=$(grep -c "multiple definition of" "$LOG" || true)
echo "  $DUP"
if [ "$DUP" -gt 0 ]; then
  grep -o "multiple definition of \`[^']*'" "$LOG" \
    | sed "s/multiple definition of \`//; s/'$//" | sort -u | sed 's/^/    /'
fi

echo
echo "=== sonstige Linkerfehler"
grep -E "^\S+ld: .*(error|Error)|collect2: error" "$LOG" \
  | grep -v "undefined reference\|multiple definition" | head -5 | sed 's/^/  /' \
  || true

echo
echo "=== Ergebnis"
grep "built \.\.\." "$LOG" | sed 's/^/  /' || echo "  nichts gebaut"

if [ -f "$D/$EX.nro" ]; then
  ls -lh "$D/$EX.nro" | awk '{print "  " $5 "  " $9}'
fi
