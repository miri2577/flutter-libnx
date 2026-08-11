#!/usr/bin/env python3
"""Entfernt mehrfach eingefuegte Horizon-Zweige aus dem Engine-Checkout.

Warum es das braucht: `replace_once` erkennt einen bereits erledigten Patch
daran, dass der Ersatztext im Ziel steht. Aendert sich dieser Ersatztext
nachtraeglich - und sei es nur in der Einrueckung -, greift die Pruefung nicht
mehr, waehrend der Anker die erste Ersetzung ueberlebt hat. Der Zweig wird dann
bei jedem Lauf erneut eingefuegt.

Funktional sind die Kopien harmlos: GN nimmt den ersten passenden Zweig, der
Praeprozessor wertet dieselbe Bedingung mehrfach aus. Sie wachsen aber mit
jedem Patch-Lauf und verdecken beim Lesen, was tatsaechlich geaendert wurde.

Dasselbe Muster wie bei dedupe-vm-sources.sh und dedupe-io-sources.sh - das
dritte Mal, dass ein nicht idempotenter Patch nachtraeglich aufgeraeumt werden
muss.
"""
import os
import re
import sys

SRC = os.environ.get("SRC", os.path.expanduser("~/engine/flutter/engine/src"))

DART = os.path.join(SRC, "flutter", "third_party", "dart", "runtime")


def dedupe_gn_branch(path: str) -> int:
    """Mehrfache `} else if (target_os == "horizon") {`-Zweige auf einen kuerzen."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    block = ('  } else if (target_os == "horizon") {\n'
             '    defines += [ "DART_TARGET_OS_HORIZON" ]\n')
    count = text.count(block)
    if count <= 1:
        print(f"    nichts zu tun: {os.path.basename(path)} ({count} Zweig)")
        return 0

    text = text.replace(block * count, block, 1)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    removed = count - 1
    print(f"    {os.path.basename(path)}: {removed} ueberzaehlige Zweige entfernt")
    return removed


def dedupe_globals_condition(path: str) -> int:
    """Mehrfaches `!defined(DART_TARGET_OS_HORIZON)` in der Zielsuche kuerzen."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    line = "    !defined(DART_TARGET_OS_HORIZON) &&      \\\n"
    count = text.count(line)
    if count == 0:
        print("    nichts zu tun: globals.h")
        return 0

    # Die letzte Bedingung der Kette traegt keinen Fortsetzungsstrich; sie
    # bleibt stehen, alle vorangehenden Kopien fallen weg.
    text = text.replace(line * count, "", 1)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(f"    globals.h: {count} ueberzaehlige Bedingungen entfernt")
    return count


def dedupe_skia_block(path: str) -> int:
    """Mehrfach eingefuegte `if (is_horizon) { ... }`-Bloecke auf einen kuerzen."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    # Der Block wird als Ganzes eingefuegt und ist deshalb woertlich
    # wiederholbar. Anfang und Ende sind eindeutig, der Inhalt wird nicht
    # geraten - es zaehlt nur, dass zwei identische Bloecke aufeinanderfolgen.
    pattern = re.compile(
        r"(if \(is_horizon\) \{\n(?:.*?\n)*?\}\n)(?=\1)", re.MULTILINE)
    removed = 0
    while True:
        new_text, n = pattern.subn("", text, count=1)
        if n == 0:
            break
        text = new_text
        removed += 1

    if removed == 0:
        print(f"    nichts zu tun: {os.path.basename(path)}")
        return 0

    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(f"    {os.path.basename(path)}: {removed} ueberzaehlige Bloecke entfernt")
    return removed


def main() -> int:
    print("==> ueberzaehlige Horizon-Zweige entfernen")
    dedupe_gn_branch(os.path.join(DART, "BUILD.gn"))
    dedupe_globals_condition(os.path.join(DART, "platform", "globals.h"))
    dedupe_skia_block(os.path.join(SRC, "flutter", "skia", "BUILD.gn"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
