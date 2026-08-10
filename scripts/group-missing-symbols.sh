#!/usr/bin/env bash
# Fasst die undefinierten Symbole des Beispiel-Links nach Bereichen zusammen.
# Einzeln abzuarbeiten waere Zeitverschwendung - die Symbole kommen in Gruppen,
# und jede Gruppe hat genau eine Ursache.
set -u
LOG="${1:-/tmp/link.log}"

if [ ! -f "$LOG" ]; then
  echo "Linkprotokoll fehlt: $LOG" >&2
  exit 1
fi

echo "=== undefinierte Symbole nach Bereich"
grep -o "undefined reference to \`[^']*'" "$LOG" \
  | sed "s/undefined reference to \`//; s/'$//" \
  | sort -u > /tmp/missing.txt

TOTAL=$(wc -l < /tmp/missing.txt)
echo "  insgesamt: $TOTAL"
echo

for group in "dart::bin::Process" "dart::bin::Namespace" \
             "dart::bin::FileSystemWatcher" "dart::bin::Directory" \
             "dart::bin::File" "dart::bin::Socket" "dart::bin::Platform" \
             "absl::" "dart::" "fml::" "flutter::"; do
  COUNT=$(grep -c "^${group}" /tmp/missing.txt || true)
  [ "$COUNT" -gt 0 ] && printf "  %-32s %s\n" "$group" "$COUNT"
done

echo
echo "=== alle Symbole"
cat /tmp/missing.txt | sed 's/^/  /'
