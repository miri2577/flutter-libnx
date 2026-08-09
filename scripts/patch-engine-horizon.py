#!/usr/bin/env python3
"""Fügt der Flutter Engine das Target `current_os = "horizon"` hinzu.

Idempotent: Ein zweiter Lauf ändert nichts.

Vorbild ist der QNX-Port, der als einziges nicht-Linux-POSIX-Target im Baum
zeigt, wie klein ein neues OS gehalten werden kann.
"""

import os
import re
import sys

SRC = os.environ.get("SRC", os.path.expanduser("~/engine/flutter/engine/src"))

HORIZON_BLOCK = '''} else if (current_os == "horizon") {
  # Nintendo Switch / Horizon OS, gebaut mit libnx und devkitA64.
  #
  # is_posix bleibt bewusst false: newlib und libnx liefern zwar große Teile
  # von POSIX (Threads, Datei-I/O, Sockets), aber weder mmap noch dlopen. Jede
  # POSIX-Annahme wird deshalb einzeln freigeschaltet statt pauschal geerbt.
  # QNX verfährt an dieser Stelle genauso.
  is_android = false
  is_chromeos = false
  is_fuchsia = false
  is_fuchsia_host = false
  is_ios = false
  is_linux = false
  is_mac = false
  is_posix = false
  is_win = false
  is_wasm = false
  is_qnx = false
  is_horizon = true
}'''


def patch_buildconfig(path: str) -> bool:
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    if "is_horizon" in text:
        print(f"    schon gepatcht: {path}")
        return False

    # 1. Jeder bestehende Zweig muss is_horizon ebenfalls setzen, sonst ist die
    #    Variable in den anderen Konfigurationen undefiniert.
    text, count = re.subn(
        r"(?m)^(\s*)is_qnx = (true|false)$",
        lambda m: f"{m.group(1)}is_qnx = {m.group(2)}\n{m.group(1)}is_horizon = false",
        text,
    )
    print(f"    is_horizon = false in {count} Zweige eingefügt")

    # 2. Den neuen Zweig direkt hinter den QNX-Zweig hängen. Der QNX-Zweig endet
    #    mit `is_horizon = false` (gerade eingefügt) gefolgt von `}`.
    anchor = 'current_os == "qnx"'
    start = text.index(anchor)
    close = text.index("\n}", start) + 1
    text = text[:close] + HORIZON_BLOCK + text[close + 1:]

    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print(f"    horizon-Zweig eingefügt: {path}")
    return True


TOOLCHAIN_SELECT = '''} else if (is_horizon) {
  host_toolchain = "//build/toolchain/linux:clang_$host_cpu"
  set_default_toolchain("//build/toolchain/horizon")
'''

TOOLCHAIN_GNI = '''# Copyright 2026 The flutter-libnx Authors.

declare_args() {
  # Wurzel der devkitPro-Installation, also das Verzeichnis, das devkitA64/
  # und libnx/ enthält. Muss beim `gn gen` gesetzt werden.
  devkitpro_root = ""
}
'''

TOOLCHAIN_BUILD = '''# Copyright 2026 The flutter-libnx Authors.
#
# Toolchain für Nintendo Switch Homebrew (Horizon OS) über devkitA64.
#
# Aufgebaut nach dem Vorbild von //build/toolchain/qnx: Beides sind
# GCC-Toolchains, weshalb `gcc_toolchain` passt und `is_clang = false` gilt.
# Der übrige Engine-Build ist auf Clang ausgelegt; QNX ist der Beleg dafür,
# dass die GCC-Pfade gepflegt sind.

import("//build/toolchain/gcc_toolchain.gni")
import("//build/toolchain/horizon/horizon.gni")

assert(devkitpro_root != "",
       "devkitpro_root muss gesetzt sein, z.B. devkitpro_root=\\"/home/user/devkitpro\\"")

_bin = "$devkitpro_root/devkitA64/bin/aarch64-none-elf"

gcc_toolchain("horizon") {
  asm = "${_bin}-gcc"
  cc = "${_bin}-gcc"
  cxx = "${_bin}-g++"

  readelf = "${_bin}-readelf"
  nm = "${_bin}-nm"
  ar = "${_bin}-ar"
  ld = "${_bin}-g++"
  strip = "${_bin}-strip"

  toolchain_cpu = "arm64"

  # Wie bei QNX: steuert nur die Namensgebung innerhalb von gcc_toolchain.gni,
  # nicht die Zielplattform. Die kommt aus current_os.
  toolchain_os = "linux"

  is_clang = false
}
'''


def patch_toolchain_selection(path: str) -> bool:
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    if "toolchain/horizon" in text:
        print("    Toolchain-Auswahl schon gepatcht")
        return False

    anchor = 'set_default_toolchain("//build/toolchain/qnx")'
    start = text.index(anchor)
    close = text.index("\n}", start) + 1
    text = text[:close] + TOOLCHAIN_SELECT + text[close + 1:]

    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    print("    Toolchain-Auswahl ergänzt")
    return True


def write_toolchain_files(src: str) -> None:
    directory = os.path.join(src, "build", "toolchain", "horizon")
    os.makedirs(directory, exist_ok=True)
    for name, content in (
        ("horizon.gni", TOOLCHAIN_GNI),
        ("BUILD.gn", TOOLCHAIN_BUILD),
    ):
        target = os.path.join(directory, name)
        with open(target, "w", encoding="utf-8") as handle:
            handle.write(content)
        print(f"    geschrieben: build/toolchain/horizon/{name}")


def main() -> int:
    buildconfig = os.path.join(SRC, "build", "config", "BUILDCONFIG.gn")
    if not os.path.exists(buildconfig):
        print(f"Nicht gefunden: {buildconfig}", file=sys.stderr)
        return 1

    print("==> build/config/BUILDCONFIG.gn")
    patch_buildconfig(buildconfig)
    patch_toolchain_selection(buildconfig)

    print("==> build/toolchain/horizon/")
    write_toolchain_files(SRC)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
