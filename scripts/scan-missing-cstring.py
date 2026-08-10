#!/usr/bin/env python3
"""Findet Engine-Quellen, die <cstring>/<climits>-Funktionen ohne Include nutzen.

Hintergrund: glibc zieht diese Header über andere Includes transitiv herein,
newlib nicht. Das sind echte Portabilitätslücken im Upstream-Code, die auf
jeder schlanken libc auffielen – und die GCC selbst mit
"this is probably fixable by adding '#include <cstring>'" quittiert.

Einzeln durch Build-Zyklen zu gehen kostet pro Datei einen Durchlauf. Dieses
Skript findet stattdessen alle Kandidaten auf einmal.
"""

import os
import re
import sys

SRC = os.environ.get("SRC", os.path.expanduser("~/engine/flutter/engine/src"))

# Nur Engine-eigener Code. third_party bringt eigene Konventionen mit.
ROOTS = [
    "flutter/assets",
    "flutter/common",
    "flutter/display_list",
    "flutter/flow",
    "flutter/fml",
    "flutter/impeller",
    "flutter/lib",
    "flutter/runtime",
    "flutter/shell",
    "flutter/txt",
]

CHECKS = (
    ("<cstring>", ("string.h", "cstring"),
     re.compile(r"\b(memcpy|memset|memmove|memcmp|strlen|strcmp|strncmp|strdup)\s*\(")),
    ("<climits>", ("limits.h", "climits"),
     re.compile(r"\b(INT_MAX|INT_MIN|UINT_MAX|LONG_MAX|LONG_MIN|ULONG_MAX|CHAR_BIT)\b")),
)


def main() -> int:
    hits = []
    for root in ROOTS:
        base = os.path.join(SRC, root)
        for dirpath, _dirnames, filenames in os.walk(base):
            for filename in filenames:
                if not filename.endswith((".cc", ".cpp")):
                    continue
                if "_unittests" in filename or filename.endswith("_test.cc"):
                    continue
                path = os.path.join(dirpath, filename)
                try:
                    with open(path, encoding="utf-8", errors="replace") as handle:
                        text = handle.read()
                except OSError:
                    continue

                for header, includes, pattern in CHECKS:
                    if not pattern.search(text):
                        continue
                    if any(f"#include <{inc}>" in text for inc in includes):
                        continue
                    hits.append((os.path.relpath(path, SRC), header))

    for path, header in sorted(hits):
        print(f"{header}\t{path}")
    print(f"\n{len(hits)} Kandidaten", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
