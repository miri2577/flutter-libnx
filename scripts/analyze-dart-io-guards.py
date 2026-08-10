#!/usr/bin/env python3
"""Prueft je dart:io-Plattformdatei, ob sie Horizon ueberhaupt im Weg steht.

Die Dateien werden bereits uebersetzt - ihr Inhalt steht aber hinter
`#if defined(DART_HOST_OS_LINUX)` und faellt deshalb weg. Die Frage ist
nicht, ob man sie ersetzt, sondern ob der Guard einfach erweitert werden
kann. Das haengt allein daran, welche Linux-eigenen Schnittstellen darin
wirklich vorkommen.
"""

import os
import re
import sys

FILES = [
    "socket_base_linux.cc",
    "socket_linux.cc",
    "sync_socket_linux.cc",
    "namespace_linux.cc",
    "platform_linux.cc",
    "process_linux.cc",
    "file_system_watcher_linux.cc",
    "security_context_linux.cc",
    "crypto_linux.cc",
    "console_linux.cc",
    "file_linux.cc",
    "directory_linux.cc",
]

# Was es auf Horizon nachweislich nicht gibt.
BLOCKERS = {
    "sys/epoll.h": "epoll",
    "sys/inotify.h": "inotify",
    "sys/timerfd.h": "timerfd",
    "sys/eventfd.h": "eventfd",
    "sys/prctl.h": "prctl",
    "sys/signalfd.h": "signalfd",
    "linux/": "linux-eigener Header",
    "sys/sendfile.h": "sendfile",
    "sys/statvfs.h": "statvfs",
    "mntent.h": "mntent",
    "ifaddrs.h": "ifaddrs",
    "net/if.h": "net/if",
    "sys/un.h": "AF_UNIX-Pfade",
    "sys/mman.h": "mmap",
}

CALL_BLOCKERS = [
    (r"\bfork\s*\(", "fork"),
    (r"\bexecvp?\s*\(", "exec"),
    (r"\bwaitpid\s*\(", "waitpid"),
    (r"\bkill\s*\(", "kill"),
    (r"\bsigaction\s*\(", "sigaction"),
    (r"\binotify_\w+\s*\(", "inotify"),
    (r"\bepoll_\w+\s*\(", "epoll"),
    (r"\bgetifaddrs\s*\(", "getifaddrs"),
    (r"\bgetentropy\s*\(", "getentropy"),
    (r"\bmmap\s*\(", "mmap"),
    (r"\bdlopen\s*\(", "dlopen"),
    (r"\bpthread_sigmask\s*\(", "pthread_sigmask"),
    (r"\bpipe2?\s*\(", "pipe"),
    (r"\bprctl\s*\(", "prctl"),
]


def analyze(path: str, name: str) -> None:
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()

    guard = "?"
    match = re.search(r"#if\s+defined\(DART_HOST_OS_\w+\)[^\n]*", text)
    if match:
        guard = match.group(0)

    found_headers = []
    for header, why in BLOCKERS.items():
        if re.search(r'#include\s+[<"]' + re.escape(header), text):
            found_headers.append(why)

    found_calls = []
    for pattern, why in CALL_BLOCKERS:
        if re.search(pattern, text):
            found_calls.append(why)

    lines = len(text.splitlines())
    horizon_ready = not found_headers and not found_calls

    mark = "GUARD ERWEITERN" if horizon_ready else "eigene Fassung"
    print(f"{name}  ({lines} Zeilen)  ->  {mark}")
    print(f"    Guard: {guard}")
    if found_headers:
        print(f"    Header: {', '.join(sorted(set(found_headers)))}")
    if found_calls:
        print(f"    Aufrufe: {', '.join(sorted(set(found_calls)))}")
    print()


def main() -> int:
    src = os.environ.get(
        "SRC", os.path.expanduser("~/engine/flutter/engine/src"))
    base = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin")

    for name in FILES:
        path = os.path.join(base, name)
        if not os.path.exists(path):
            print(f"{name}  ->  fehlt\n")
            continue
        analyze(path, name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
