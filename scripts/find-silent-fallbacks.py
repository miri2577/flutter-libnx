#!/usr/bin/env python3
"""Sucht Plattformweichen, die fuer Horizon still ins Leere laufen.

MessageLoopImpl::Create() war so ein Fall: eine #if-Kette ueber alle bekannten
Betriebssysteme, deren #else-Zweig nullptr zurueckgibt. Das linkt anstandslos
und faellt erst zur Laufzeit auf. Solche Stellen sind gefaehrlicher als ein
Compilerfehler, weil sie nichts melden.

Gesucht werden Praeprozessorketten, die mindestens zwei bekannte
Plattformmakros pruefen und einen #else-Zweig haben, in dem etwas
Verdaechtiges steht - oder die gar kein #else haben, was bei einer Zuweisung
oder Rueckgabe ebenfalls ein Loch bedeutet.
"""

import os
import re
import sys

ROOTS = [
    "flutter/fml",
    "flutter/shell/common",
    "flutter/shell/platform/embedder",
    "flutter/runtime",
    "flutter/common",
    "flutter/assets",
    "flutter/lib/ui",
]

PLATFORM_MACROS = re.compile(
    r"\b(FML_OS_(MACOSX|ANDROID|LINUX|WIN|IOS)|OS_FUCHSIA|"
    r"DART_HOST_OS_\w+|SK_BUILD_FOR_\w+|__linux__|__APPLE__|_WIN32)\b"
)

# Was im #else-Zweig als stiller Ausfall gilt.
SUSPICIOUS = re.compile(
    r"return\s+(nullptr|NULL|false|\{\}|-1)\s*;|"
    r"^\s*#\s*else\s*$\n\s*#\s*endif"
)


def scan_file(path: str, rel: str) -> list:
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.readlines()
    except OSError:
        return []

    findings = []
    # Ketten sammeln: von #if(def) bis #endif auf derselben Ebene.
    stack = []
    for index, line in enumerate(lines):
        stripped = line.strip()
        if re.match(r"#\s*if", stripped):
            stack.append({
                "start": index,
                "platform": bool(PLATFORM_MACROS.search(stripped)),
                "branches": 1,
                "has_else": False,
                "else_line": None,
            })
        elif re.match(r"#\s*elif", stripped) and stack:
            stack[-1]["branches"] += 1
            if PLATFORM_MACROS.search(stripped):
                stack[-1]["platform"] = True
        elif re.match(r"#\s*else", stripped) and stack:
            stack[-1]["has_else"] = True
            stack[-1]["else_line"] = index
        elif re.match(r"#\s*endif", stripped) and stack:
            block = stack.pop()
            if not block["platform"] or block["branches"] < 2:
                continue
            if block["has_else"]:
                body = "".join(lines[block["else_line"] + 1:index])
                if SUSPICIOUS.search(body):
                    findings.append((
                        rel, block["else_line"] + 1,
                        "else-Zweig liefert einen Leerwert",
                        body.strip().splitlines()[:3],
                    ))
            else:
                # Kein #else. Nur interessant, wenn die Kette Rueckgabewerte
                # oder Zuweisungen enthaelt - sonst ist das Fehlen normal.
                body = "".join(lines[block["start"]:index])
                if re.search(r"^\s*return\s+\S", body, re.M):
                    findings.append((
                        rel, block["start"] + 1,
                        "Kette ohne else, aber mit return",
                        body.strip().splitlines()[:3],
                    ))
    return findings


def main() -> int:
    src = os.environ.get(
        "SRC", os.path.expanduser("~/engine/flutter/engine/src"))

    total = []
    for root in ROOTS:
        base = os.path.join(src, root)
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [d for d in dirnames if d not in
                           ("fixtures", "testing")]
            for name in filenames:
                if not name.endswith((".cc", ".cpp", ".h", ".mm")):
                    continue
                if "unittest" in name or "_test" in name or "benchmark" in name:
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, src)
                total.extend(scan_file(path, rel))

    if not total:
        print("keine verdaechtigen Plattformweichen gefunden")
        return 0

    print(f"{len(total)} verdaechtige Stellen\n")
    for rel, line, why, preview in total:
        print(f"{rel}:{line}")
        print(f"    {why}")
        for text in preview:
            print(f"    | {text.strip()}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
