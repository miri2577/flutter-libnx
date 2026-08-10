#!/usr/bin/env python3
"""Wie analyze-dart-io-guards.py, aber fuer die POSIX-Ebene.

dart:io ist zweistufig aufgebaut: `*_posix.cc` traegt die gemeinsame
Unix-Implementierung, `*_linux.cc` nur das, was sich zwischen den Unixen
unterscheidet. Wer bloss die Linux-Ebene freischaltet, bekommt eine Handvoll
Symbole und wundert sich ueber den Rest - genau das ist hier passiert:
socket_base_linux.cc definiert Multicast und Lookup, waehrend Read, Write und
Close in socket_base_posix.cc liegen.
"""

import os
import re
import sys

BLOCKERS = {
    "sys/epoll.h": "epoll",
    "sys/inotify.h": "inotify",
    "sys/timerfd.h": "timerfd",
    "sys/eventfd.h": "eventfd",
    "sys/prctl.h": "prctl",
    "sys/signalfd.h": "signalfd",
    "sys/sendfile.h": "sendfile",
    "sys/statvfs.h": "statvfs",
    "mntent.h": "mntent",
    "ifaddrs.h": "ifaddrs",
    "sys/un.h": "AF_UNIX-Pfade",
    "sys/mman.h": "mmap",
    "dlfcn.h": "dlopen",
}

CALL_BLOCKERS = [
    (r"\bfork\s*\(", "fork"),
    (r"\bexecvp?e?\s*\(", "exec"),
    (r"\bwaitpid\s*\(", "waitpid"),
    (r"\bsigaction\s*\(", "sigaction"),
    (r"\bpthread_sigmask\s*\(", "pthread_sigmask"),
    (r"\binotify_\w+\s*\(", "inotify"),
    (r"\bepoll_\w+\s*\(", "epoll"),
    (r"\bgetifaddrs\s*\(", "getifaddrs"),
    (r"\bgetentropy\s*\(", "getentropy"),
    (r"\bmmap\s*\(", "mmap"),
    (r"\bdlopen\s*\(", "dlopen"),
    (r"\bpipe2?\s*\(", "pipe"),
    (r"\bprctl\s*\(", "prctl"),
    (r"\bfstatat\s*\(", "fstatat"),
    (r"\breadlinkat\s*\(", "readlinkat"),
    (r"\bsymlinkat\s*\(", "symlinkat"),
    (r"\butimensat\s*\(", "utimensat"),
    (r"\bpread\s*\(", "pread"),
]


def main() -> int:
    src = os.environ.get(
        "SRC", os.path.expanduser("~/engine/flutter/engine/src"))
    base = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin")

    names = sorted(n for n in os.listdir(base) if n.endswith("_posix.cc"))
    print(f"{len(names)} POSIX-Dateien in dart/runtime/bin\n")

    for name in names:
        path = os.path.join(base, name)
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()

        has_horizon = "DART_HOST_OS_HORIZON" in text
        guard = "?"
        match = re.search(r"#if\s+defined\(DART_HOST_OS_\w+\)[^\n]*", text)
        if match:
            guard = match.group(0).rstrip("\\ ")

        headers = sorted({why for header, why in BLOCKERS.items()
                          if re.search(r'#include\s+[<"]' + re.escape(header),
                                       text)})
        calls = sorted({why for pattern, why in CALL_BLOCKERS
                        if re.search(pattern, text)})

        if has_horizon:
            mark = "erledigt"
        elif not headers and not calls:
            mark = "GUARD ERWEITERN"
        else:
            mark = "pruefen"

        print(f"{name}  ({len(text.splitlines())} Zeilen)  ->  {mark}")
        print(f"    {guard}")
        if headers:
            print(f"    Header:  {', '.join(headers)}")
        if calls:
            print(f"    Aufrufe: {', '.join(calls)}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
