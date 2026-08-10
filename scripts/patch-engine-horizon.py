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


# Achtung: Beide Einfügeblöcke ersetzen die schließende Klammer des Zweigs, an
# den sie angehängt werden. Sie müssen deshalb selbst mit `}` enden. Genau das
# wurde zweimal vergessen und hat jeweils einen Syntaxfehler erzeugt.
TOOLCHAIN_SELECT = '''} else if (is_horizon) {
  host_toolchain = "//build/toolchain/linux:clang_$host_cpu"
  set_default_toolchain("//build/toolchain/horizon")
}'''

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


def replace_once(path: str, old: str, new: str, label: str) -> bool:
    """Wörtliche Ersetzung. Sicherer als das Herumschneiden an Klammern."""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    # Zwei Faelle, die sich beissen:
    #   * Bei einer leeren Ersetzung ist `new in text` immer wahr.
    #   * Beim Einfuegen bleibt der Anker erhalten, `old in text` also ebenfalls.
    # Deshalb zuerst auf das Ergebnis pruefen, aber nur wenn es nicht leer ist.
    if new and new in text:
        print(f"    schon gepatcht: {label}")
        return False
    if old not in text:
        print(f"    ANKER NICHT GEFUNDEN in {label}", file=sys.stderr)
        return False

    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text.replace(old, new, 1))
    print(f"    gepatcht: {label}")
    return True


def patch_shell_platform(src: str) -> None:
    path = os.path.join(src, "flutter", "shell", "platform", "BUILD.gn")
    # QNX baut hier gar nichts; der Embedder haengt separat unter
    # //flutter/shell/platform/embedder. Fuer Horizon gilt dasselbe.
    replace_once(
        path,
        "  } else if (is_qnx) {\n    deps = []\n  } else {",
        "  } else if (is_qnx) {\n    deps = []\n"
        "  } else if (is_horizon) {\n    deps = []\n  } else {",
        "flutter/shell/platform/BUILD.gn",
    )


def patch_compiler_config(src: str) -> None:
    path = os.path.join(src, "build", "config", "compiler", "BUILD.gn")

    # devkitpro_root wird weiter unten fuer Include- und Architekturflags
    # gebraucht.
    replace_once(
        path,
        'import("//build/config/android/config.gni")\n',
        'import("//build/config/android/config.gni")\n'
        'import("//build/toolchain/horizon/horizon.gni")\n',
        "build/config/compiler/BUILD.gn (Import horizon.gni)",
    )

    # devkitPro setzt __SWITCH__ sonst ueber sein Makefile (switch_rules).
    # GN baut nicht darueber, also muessen wir es selbst mitgeben - sonst
    # faellt build_config.h in seinen #error-Zweig und die Dart-VM kann ihr
    # Host-OS nicht bestimmen.
    replace_once(
        path,
        '  if (is_qnx) {\n    defines += [\n      "_XOPEN_SOURCE=700",',
        '  if (is_horizon) {\n'
        '    defines += [\n'
        '      "__SWITCH__",\n'
        '\n'
        '      # ICU waehlt seine Dateiabbildung ueber U_HAVE_MMAP. Ohne mmap\n'
        '      # faellt es auf MAP_STDIO zurueck - stdio plus uprv_malloc,\n'
        '      # genau der fuer solche Plattformen vorgesehene Weg\n'
        '      # (source/common/umapfile.h:41-49).\n'
        '      "U_HAVE_MMAP=0",\n'
        '\n'
        '      # Skias Plattformerkennung (SkFeatures.h) faellt fuer unbekannte\n'
        '      # Systeme auf SK_BUILD_FOR_MAC zurueck - und damit auf Grand\n'
        '      # Central Dispatch, das es hier nicht gibt. SK_BUILD_FOR_UNIX ist\n'
        '      # Skias Bezeichnung fuer "POSIX-artig" und trifft zu: newlib\n'
        '      # liefert die benutzten Schnittstellen, etwa POSIX-Semaphoren.\n'
        '      "SK_BUILD_FOR_UNIX",\n'
        '\n'
        '      # glibc fuehrt fuer Large File Support eine zweite Namensfamilie\n'
        '      # (stat64, openat64, ino64_t ...). newlib braucht sie nicht,\n'
        '      # weil off_t und ino_t dort ohnehin 64 Bit breit sind - es gibt\n'
        '      # also nur die unsuffigierte Fassung. Dart benutzt durchgaengig\n'
        '      # die 64er-Namen.\n'
        '      #\n'
        '      # Die Abbildung ist keine Notloesung, sondern die Feststellung\n'
        '      # einer Identitaet: Auf dieser Plattform *sind* stat und stat64\n'
        '      # dasselbe. Zentral hier statt an rund zwanzig Einzelstellen.\n'
        '      "stat64=stat",\n'
        '      "fstat64=fstat",\n'
        '      "lstat64=lstat",\n'
        '      "fstatat64=fstatat",\n'
        '      "openat64=openat",\n'
        '      "open64=open",\n'
        '      "creat64=creat",\n'
        '      "lseek64=lseek",\n'
        '      "ftruncate64=ftruncate",\n'
        '      "truncate64=truncate",\n'
        '      "readdir64=readdir",\n'
        '      "dirent64=dirent",\n'
        '      "ino64_t=ino_t",\n'
        '      "off64_t=off_t",\n'
        '    ]\n'
        '\n'
        '    cflags += [\n'
        '      # libnx-Header. Sie liegen nicht in newlib: arpa/inet.h,\n'
        '      # netdb.h, poll.h und die Switch-APIs kommen von hier.\n'
        '      "-I" + devkitpro_root + "/libnx/include",\n'
        '\n'
        '      # Dieselben Architekturflags, die devkitPro in switch_rules\n'
        '      # setzt. -mtp=soft ist dabei nicht optional: Horizon erlaubt\n'
        '      # den Zugriff auf das Thread-Pointer-Register nicht direkt.\n'
        '      "-march=armv8-a+crc+crypto",\n'
        '      "-mtune=cortex-a57",\n'
        '      "-mtp=soft",\n'
        '      "-ftls-model=local-exec",\n'
        '      "-fPIE",\n'
        '    ]\n'
        '  }\n\n'
        '  if (is_qnx) {\n    defines += [\n      "_XOPEN_SOURCE=700",',
        "build/config/compiler/BUILD.gn",
    )


def patch_dart_runtime(src: str) -> None:
    path = os.path.join(
        src, "flutter", "third_party", "dart", "runtime", "BUILD.gn")
    replace_once(
        path,
        '  } else if (target_os == "win") {\n'
        '    defines += [ "DART_TARGET_OS_WINDOWS" ]',
        '  } else if (target_os == "horizon") {\n'
        '    defines += [ "DART_TARGET_OS_HORIZON" ]\n'
        '  } else if (target_os == "win") {\n'
        '    defines += [ "DART_TARGET_OS_WINDOWS" ]',
        "third_party/dart/runtime/BUILD.gn",
    )


def patch_cxx_disable_modules(src: str) -> None:
    path = os.path.join(src, "build", "config", "compiler", "BUILD.gn")
    # -Xclang und -fno-cxx-modules kennt GCC nicht. Der Kommentar daneben sagt
    # selbst, dass es um ObjC-Module in bestimmten Clang-Versionen geht - fuer
    # eine GCC-Toolchain ist beides gegenstandslos. -fno-modules bleibt, das
    # versteht GCC.
    replace_once(
        path,
        '  disable_modules_flags = [\n'
        '    "-fno-modules",\n'
        '\n'
        '    # Some Clang versions do not disable ObjC modules (as tested by\n'
        '    # __has_feature(objc_modules)) when just -fno-modules is present.\n'
        '    "-Xclang",\n'
        '    "-fno-cxx-modules",\n'
        '  ]',
        '  disable_modules_flags = [ "-fno-modules" ]\n'
        '  if (is_clang) {\n'
        '    # Some Clang versions do not disable ObjC modules (as tested by\n'
        '    # __has_feature(objc_modules)) when just -fno-modules is present.\n'
        '    # GCC kennt weder -Xclang noch -fno-cxx-modules.\n'
        '    disable_modules_flags += [\n'
        '      "-Xclang",\n'
        '      "-fno-cxx-modules",\n'
        '    ]\n'
        '  }',
        "build/config/compiler/BUILD.gn (cxx_disable_modules)",
    )


def patch_fml_build_config(src: str) -> None:
    path = os.path.join(src, "flutter", "fml", "build_config.h")
    # __SWITCH__ setzt die GN-Compilerkonfiguration (siehe patch_compiler_config).
    #
    # FML_OS_POSIX wird bewusst NICHT gesetzt, obwohl newlib und libnx grosse
    # Teile von POSIX liefern. Die POSIX-Quellen von fml setzen mmap
    # (mapping_posix.cc) und dlopen (native_library_posix.cc) voraus, und beides
    # fehlt auf Horizon. Jede POSIX-Faehigkeit wird einzeln freigeschaltet -
    # das ist der ganze Sinn eines eigenen Targets.
    replace_once(
        path,
        "#elif defined(__EMSCRIPTEN__)\n"
        "#define FML_OS_EMSCRIPTEN\n"
        "#else",
        "#elif defined(__EMSCRIPTEN__)\n"
        "#define FML_OS_EMSCRIPTEN\n"
        "#elif defined(__SWITCH__)\n"
        "#define FML_OS_HORIZON 1\n"
        "#else",
        "flutter/fml/build_config.h",
    )


def patch_werror(src: str) -> None:
    path = os.path.join(src, "build", "config", "compiler", "BUILD.gn")
    # QNX nimmt sich von -Werror bereits aus, und aus demselben Grund: Die
    # Warnungsfreiheit der Engine ist gegen Clang geprueft, nicht gegen GCC.
    # GCC meldet etwa nach einem switch ueber alle Enum-Werte trotzdem
    # "control reaches end of non-void function" (fml/cpu_affinity.cc:86).
    #
    # Das ist eine Bootstrap-Massnahme, kein Dauerzustand: Sobald die
    # Portierung steht, gehoert geprueft, welche dieser Warnungen echte Fehler
    # sind. Die Alternative waere der Clang-Weg ueber custom_toolchain
    # (siehe docs/gn-target-horizon.md) - der diese Fehlerklasse komplett
    # vermeiden wuerde.
    replace_once(
        path,
        "  if (!is_qnx) {\n"
        '    default_warning_flags += [\n'
        '      "-Werror",  # Warnings as errors.\n'
        "    ]\n"
        "  }",
        "  if (!is_qnx && !is_horizon) {\n"
        '    default_warning_flags += [\n'
        '      "-Werror",  # Warnings as errors.\n'
        "    ]\n"
        "  }",
        "build/config/compiler/BUILD.gn (-Werror)",
    )


# Der Engine-Code verlässt sich an vielen Stellen darauf, dass glibc <cstring>
# und <climits> transitiv mitzieht. newlib tut das nicht. Statt eine Liste von
# Hand zu pflegen – der erste Anlauf übersah sämtliche Header – wird der Baum
# durchsucht und der fehlende Include ergänzt.
#
# Das ist mechanisch und nachweislich harmlos: Ein zusätzlicher Standard-Include
# ändert kein Verhalten. GCC schlägt die Korrektur bei jedem dieser Fehler von
# sich aus vor.
INCLUDE_SCAN_ROOTS = (
    "flutter/assets",
    "flutter/common",
    "flutter/display_list",
    "flutter/flow",
    "flutter/fml",
    "flutter/impeller",
    "flutter/lib",
    "flutter/runtime",
    "flutter/shell/common",
    "flutter/shell/platform/embedder",
    "flutter/txt",
    # Der Dart-Baum hat dieselbe Schwaeche: <climits> und <cstring> kommen
    # unter glibc transitiv mit, unter newlib nicht.
    "flutter/third_party/dart/runtime/lib",
    "flutter/third_party/dart/runtime/vm",
    "flutter/third_party/dart/runtime/bin",
    "flutter/third_party/dart/runtime/platform",
)

INCLUDE_CHECKS = (
    ("cstring", ("string.h", "cstring"),
     re.compile(r"\b(memcpy|memset|memmove|memcmp|strlen|strcmp|strncmp|strdup)\s*\(")),
    ("climits", ("limits.h", "climits"),
     re.compile(r"\b(INT_MAX|INT_MIN|UINT_MAX|LONG_MAX|LONG_MIN|ULONG_MAX|"
                r"LLONG_MAX|LLONG_MIN|ULLONG_MAX|SHRT_MAX|SHRT_MIN|CHAR_BIT)\b")),
)


def add_include(src: str, rel_path: str, header: str) -> bool:
    """Fuegt einen Standard-Include nach dem ersten vorhandenen #include ein."""
    path = os.path.join(src, rel_path)
    if not os.path.exists(path):
        return False

    with open(path, encoding="utf-8") as handle:
        lines = handle.readlines()

    needle = f"#include <{header}>"
    if any(line.startswith(needle) for line in lines):
        return False

    # Nach dem ersten #include einfuegen. In Headern steht davor der
    # Include-Guard, weshalb die erste Zeile nicht taugt.
    for index, line in enumerate(lines):
        if line.startswith("#include"):
            lines.insert(index + 1, f"{needle}\n")
            break
    else:
        print(f"    KEIN #include GEFUNDEN in {rel_path}", file=sys.stderr)
        return False

    with open(path, "w", encoding="utf-8") as handle:
        handle.writelines(lines)
    return True


def patch_missing_includes(src: str) -> None:
    """Fehlende Standard-Includes, die unter glibc transitiv mitkamen.

    newlib zieht deutlich weniger indirekt herein als glibc. Das sind echte
    Portabilitaetsluecken im Upstream-Code, keine Horizon-Eigenheiten - sie
    faenden sich auf jeder schlanken libc.
    """
    replace_once(
        os.path.join(src, "flutter", "fml", "message_loop_task_queues.cc"),
        '#include "flutter/fml/message_loop_task_queues.h"\n',
        '#include "flutter/fml/message_loop_task_queues.h"\n'
        "\n"
        "#include <climits>  // ULONG_MAX; unter glibc transitiv, unter newlib nicht\n",
        "flutter/fml/message_loop_task_queues.cc",
    )

    replace_once(
        os.path.join(src, "flutter", "display_list", "dl_storage.cc"),
        '#include "flutter/display_list/dl_storage.h"\n',
        '#include "flutter/display_list/dl_storage.h"\n'
        "\n"
        "#include <cstring>  // memset; unter glibc transitiv, unter newlib nicht\n",
        "flutter/display_list/dl_storage.cc",
    )

    added = 0
    for root in INCLUDE_SCAN_ROOTS:
        base = os.path.join(src, root)
        for dirpath, _dirnames, filenames in os.walk(base):
            for filename in sorted(filenames):
                if not filename.endswith((".cc", ".cpp", ".h")):
                    continue
                if "_unittest" in filename or filename.endswith("_test.cc"):
                    continue
                path = os.path.join(dirpath, filename)
                with open(path, encoding="utf-8", errors="replace") as handle:
                    text = handle.read()

                for header, includes, pattern in INCLUDE_CHECKS:
                    if not pattern.search(text):
                        continue
                    if any(f"#include <{inc}>" in text for inc in includes):
                        continue
                    if add_include(src, os.path.relpath(path, src), header):
                        added += 1
    print(f"    {added} fehlende Includes ergänzt")


def patch_fml_backtrace(src: str) -> None:
    """Horizon hat keine execinfo.h und keine Stack-Unwinding-API.

    Statt einen Backtrace zu erfinden, liefert der Pfad null Frames. Der
    Aufrufer bekommt damit eine leere statt einer falschen Ausgabe.
    """
    path = os.path.join(src, "flutter", "fml", "backtrace.cc")

    replace_once(
        path,
        "#else  // FML_OS_WIN\n"
        "#include <execinfo.h>\n"
        "#endif  // FML_OS_WIN",
        "#elif defined(FML_OS_HORIZON)\n"
        "// Horizon/libnx kennt kein execinfo.h. Siehe Backtrace() unten.\n"
        "#else  // FML_OS_WIN\n"
        "#include <execinfo.h>\n"
        "#endif  // FML_OS_WIN",
        "flutter/fml/backtrace.cc (Include)",
    )

    replace_once(
        path,
        "#if FML_OS_WIN\n"
        "  return CaptureStackBackTrace(0, size, symbols, NULL);\n"
        "#else\n"
        "  return ::backtrace(symbols, size);\n"
        "#endif  // FML_OS_WIN",
        "#if FML_OS_WIN\n"
        "  return CaptureStackBackTrace(0, size, symbols, NULL);\n"
        "#elif defined(FML_OS_HORIZON)\n"
        "  // Horizon bietet im Homebrew-Kontext keine Unwinding-API. Bewusst\n"
        "  // null Frames: eine leere Ausgabe ist ehrlicher als eine erfundene.\n"
        "  (void)symbols;\n"
        "  (void)size;\n"
        "  return 0;\n"
        "#else\n"
        "  return ::backtrace(symbols, size);\n"
        "#endif  // FML_OS_WIN",
        "flutter/fml/backtrace.cc (Backtrace)",
    )


def patch_fml_platform_sources(src: str, repo: str) -> None:
    """Ersetzt die POSIX-Bausteine, die auf Horizon nicht funktionieren.

    fml/BUILD.gn kennt nur `is_win` und `sonst POSIX`. QNX kommt damit durch,
    weil es mmap und dlopen hat. Horizon hat beides nicht.
    """
    import shutil

    target_dir = os.path.join(src, "flutter", "fml", "platform", "horizon")
    os.makedirs(target_dir, exist_ok=True)
    source_dir = os.path.join(repo, "patches", "flutter-engine", "files",
                              "fml", "platform", "horizon")
    for name in ("mapping_horizon.cc", "native_library_horizon.cc"):
        shutil.copyfile(os.path.join(source_dir, name),
                        os.path.join(target_dir, name))
        print(f"    kopiert: fml/platform/horizon/{name}")

    replace_once(
        os.path.join(src, "flutter", "fml", "BUILD.gn"),
        '      "platform/posix/process_posix.cc",\n'
        "    ]\n"
        "  }",
        '      "platform/posix/process_posix.cc",\n'
        "    ]\n"
        "\n"
        "    if (is_horizon) {\n"
        "      # Horizon hat weder mmap noch dlopen.\n"
        "      sources -= [\n"
        '        "platform/posix/mapping_posix.cc",\n'
        '        "platform/posix/native_library_posix.cc",\n'
        "      ]\n"
        "      sources += [\n"
        '        "platform/horizon/mapping_horizon.cc",\n'
        '        "platform/horizon/native_library_horizon.cc",\n'
        "      ]\n"
        "    }\n"
        "  }",
        "flutter/fml/BUILD.gn",
    )

    # file_posix.cc bindet sys/mman.h ein, benutzt daraus aber nichts. Der
    # Include ist schlicht tot - Entfernen ist auch fuer andere Plattformen
    # korrekt.
    replace_once(
        os.path.join(src, "flutter", "fml", "platform", "posix",
                     "file_posix.cc"),
        "#include <sys/mman.h>\n",
        "",
        "flutter/fml/platform/posix/file_posix.cc (toter Include)",
    )


def patch_cxx_std(src: str) -> None:
    path = os.path.join(src, "build", "config", "compiler", "BUILD.gn")
    # -std=c++20 setzt __STRICT_ANSI__, und newlib blendet daraufhin POSIX-
    # Erweiterungen wie strdup aus - der Engine-Code benutzt sie aber
    # (fml/platform/posix/posix_wrappers_posix.cc).
    #
    # devkitPro baut aus genau diesem Grund selbst mit GNU-Dialekten
    # (switch_rules verwendet -std=gnu++...). Wir folgen dem.
    replace_once(
        path,
        "  if (is_win) {\n"
        '    cc_std = [ "/std:c++20" ]\n'
        "  } else {\n"
        '    cc_std = [ "-std=c++20" ]\n'
        "  }",
        "  if (is_win) {\n"
        '    cc_std = [ "/std:c++20" ]\n'
        "  } else if (is_horizon) {\n"
        "    # GNU-Dialekt, damit newlib POSIX-Erweiterungen sichtbar laesst.\n"
        '    cc_std = [ "-std=gnu++20" ]\n'
        "  } else {\n"
        '    cc_std = [ "-std=c++20" ]\n'
        "  }",
        "build/config/compiler/BUILD.gn (C++-Dialekt)",
    )


def patch_absl_gettid(src: str) -> None:
    path = os.path.join(src, "third_party", "abseil-cpp", "absl", "base",
                        "internal", "sysinfo.cc")
    # Der Rueckfallpfad setzt voraus, dass pthread_t arithmetisch ist - der
    # Kommentar dort sagt selbst, dass Plattformen ohne diese Eigenschaft
    # weiter oben behandelt gehoeren. Auf Horizon ist pthread_t ein Zeiger auf
    # die Thread-Struktur von libnx.
    replace_once(
        path,
        "#else\n"
        "\n"
        "// Fallback implementation of `GetTID` using `pthread_self`.",
        "#elif defined(__SWITCH__)\n"
        "\n"
        "// Auf Horizon ist pthread_t ein Zeiger auf die Thread-Struktur von\n"
        "// libnx und damit nicht arithmetisch. Die Adresse ist innerhalb des\n"
        "// Prozesses eindeutig; die Verkuerzung auf pid_t kann theoretisch\n"
        "// kollidieren, weil nur die unteren 32 Bit uebrig bleiben.\n"
        "pid_t GetTID() {\n"
        "  return static_cast<pid_t>(reinterpret_cast<uintptr_t>(pthread_self()));\n"
        "}\n"
        "\n"
        "#else\n"
        "\n"
        "// Fallback implementation of `GetTID` using `pthread_self`.",
        "abseil-cpp sysinfo.cc (GetTID)",
    )


def patch_absl_elf_mem_image(src: str) -> None:
    path = os.path.join(src, "third_party", "abseil-cpp", "absl", "debugging",
                        "internal", "elf_mem_image.h")
    # Der ELF-Symbolizer braucht link.h und die glibc-Erweiterungen fuer den
    # dynamischen Linker. Horizon hat weder das eine noch das andere. Die Liste
    # schliesst QNX, Haiku, VxWorks und weitere aus demselben Grund bereits aus.
    replace_once(
        path,
        '#if defined(__ELF__) && !defined(__OpenBSD__) && !defined(__QNX__) &&    \\\n',
        '#if defined(__ELF__) && !defined(__OpenBSD__) && !defined(__QNX__) &&    \\\n'
        '    !defined(__SWITCH__) &&                                          \\\n',
        "abseil-cpp elf_mem_image.h",
    )


def patch_dart_globals(src: str) -> None:
    """Traegt Horizon in die vier OS-Erkennungen der Dart-VM ein.

    DART_HOST_OS_* beschreibt das System, auf dem die VM laeuft;
    DART_TARGET_OS_* das System, fuer das AOT-Code erzeugt wird. Fuer uns ist
    beides Horizon.
    """
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime",
                        "platform", "globals.h")

    # 1. Host-OS aus dem Compilermakro.
    replace_once(
        path,
        "#elif defined(__Fuchsia__)\n"
        "#define DART_HOST_OS_FUCHSIA\n"
        "\n"
        "#elif !defined(DART_HOST_OS_FUCHSIA)\n"
        "#error Automatic target os detection failed.",
        "#elif defined(__Fuchsia__)\n"
        "#define DART_HOST_OS_FUCHSIA\n"
        "\n"
        "#elif defined(__SWITCH__)\n"
        "// Nintendo Switch / Horizon OS ueber libnx. __SWITCH__ setzt die\n"
        "// GN-Compilerkonfiguration, nicht der Compiler selbst.\n"
        "#define DART_HOST_OS_HORIZON 1\n"
        "\n"
        "#elif !defined(DART_HOST_OS_FUCHSIA)\n"
        "#error Automatic target os detection failed.",
        "dart globals.h (Host-OS)",
    )

    # 2a. Horizon in die Liste der bereits gesetzten Target-OS aufnehmen.
    replace_once(
        path,
        "#if !defined(DART_TARGET_OS_ANDROID) && !defined(DART_TARGET_OS_FUCHSIA) &&    \\\n"
        "    !defined(DART_TARGET_OS_MACOS_IOS) && !defined(DART_TARGET_OS_LINUX) &&    \\\n"
        "    !defined(DART_TARGET_OS_MACOS) && !defined(DART_TARGET_OS_WINDOWS)",
        "#if !defined(DART_TARGET_OS_ANDROID) && !defined(DART_TARGET_OS_FUCHSIA) &&    \\\n"
        "    !defined(DART_TARGET_OS_MACOS_IOS) && !defined(DART_TARGET_OS_LINUX) &&    \\\n"
        "    !defined(DART_TARGET_OS_MACOS) && !defined(DART_TARGET_OS_WINDOWS) &&      \\\n"
        "    !defined(DART_TARGET_OS_HORIZON)",
        "dart globals.h (Target-OS-Liste)",
    )

    # 2b. Rueckfall auf das Host-OS, wenn kein Target gesetzt ist.
    replace_once(
        path,
        "#elif defined(DART_HOST_OS_WINDOWS)\n"
        "#define DART_TARGET_OS_WINDOWS 1\n"
        "#else\n"
        "#error Automatic target OS detection failed.",
        "#elif defined(DART_HOST_OS_WINDOWS)\n"
        "#define DART_TARGET_OS_WINDOWS 1\n"
        "#elif defined(DART_HOST_OS_HORIZON)\n"
        "#define DART_TARGET_OS_HORIZON 1\n"
        "#else\n"
        "#error Automatic target OS detection failed.",
        "dart globals.h (Target-OS-Rueckfall)",
    )

    # 3. Klartextnamen.
    replace_once(
        path,
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#define kHostOperatingSystemName "windows"\n'
        "#else\n"
        "#error Host operating system detection failed.",
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#define kHostOperatingSystemName "windows"\n'
        '#elif defined(DART_HOST_OS_HORIZON)\n'
        '#define kHostOperatingSystemName "horizon"\n'
        "#else\n"
        "#error Host operating system detection failed.",
        "dart globals.h (kHostOperatingSystemName)",
    )

    replace_once(
        path,
        '#elif defined(DART_TARGET_OS_WINDOWS)\n'
        '#define kTargetOperatingSystemName "windows"\n'
        "#else\n"
        "#error Target operating system detection failed.",
        '#elif defined(DART_TARGET_OS_WINDOWS)\n'
        '#define kTargetOperatingSystemName "windows"\n'
        '#elif defined(DART_TARGET_OS_HORIZON)\n'
        '#define kTargetOperatingSystemName "horizon"\n'
        "#else\n"
        "#error Target operating system detection failed.",
        "dart globals.h (kTargetOperatingSystemName)",
    )


def patch_dart_platform_headers(src: str, repo: str) -> None:
    """Die vier plattformabhängigen Header der Dart-VM.

    Zwei davon brauchen nur einen zusätzlichen Zweig, weil libnx über die
    Newlib-Syscalls echte pthreads liefert. Die anderen beiden verlangen eine
    eigene Datei.
    """
    import shutil

    runtime = os.path.join(src, "flutter", "third_party", "dart", "runtime")
    files = os.path.join(repo, "patches", "flutter-engine", "files", "dart")

    for rel, name in (
        (os.path.join("platform"), "utils_horizon.h"),
        (os.path.join("vm"), "os_thread_horizon.h"),
    ):
        shutil.copyfile(os.path.join(files, rel, name),
                        os.path.join(runtime, rel, name))
        print(f"    kopiert: dart/runtime/{rel}/{name}")

    # platform/utils.h – eigene Datei.
    replace_once(
        os.path.join(runtime, "platform", "utils.h"),
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "platform/utils_win.h"\n'
        "#else\n"
        "#error Unknown target os.",
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "platform/utils_win.h"\n'
        "#elif defined(DART_HOST_OS_HORIZON)\n"
        '#include "platform/utils_horizon.h"\n'
        "#else\n"
        "#error Unknown target os.",
        "dart platform/utils.h",
    )

    # vm/os_thread.h – eigene Datei.
    replace_once(
        os.path.join(runtime, "vm", "os_thread.h"),
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "vm/os_thread_win.h"\n'
        "#else\n"
        "#error Unknown target os.",
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "vm/os_thread_win.h"\n'
        "#elif defined(DART_HOST_OS_HORIZON)\n"
        '#include "vm/os_thread_horizon.h"\n'
        "#else\n"
        "#error Unknown target os.",
        "dart vm/os_thread.h",
    )

    # platform/threads.h und platform/synchronization.h: Horizon einfach in die
    # pthread-Zweige aufnehmen. Das Muster kommt in threads.h dreimal vor.
    pthread_condition = (
        "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_FUCHSIA) ||            \\\n"
        "    defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_ANDROID)"
    )
    pthread_condition_with_horizon = (
        "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_FUCHSIA) ||            \\\n"
        "    defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_ANDROID) ||            \\\n"
        "    defined(DART_HOST_OS_HORIZON)"
    )
    threads_path = os.path.join(runtime, "platform", "threads.h")
    with open(threads_path, encoding="utf-8") as handle:
        text = handle.read()
    if "DART_HOST_OS_HORIZON" in text:
        print("    schon gepatcht: dart platform/threads.h")
    else:
        count = text.count(pthread_condition)
        text = text.replace(pthread_condition, pthread_condition_with_horizon)
        with open(threads_path, "w", encoding="utf-8") as handle:
            handle.write(text)
        print(f"    gepatcht: dart platform/threads.h ({count} Stellen)")

    replace_once(
        os.path.join(runtime, "platform", "synchronization.h"),
        "#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_FUCHSIA) ||          \\\n"
        "    defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_ANDROID)",
        "#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_FUCHSIA) ||          \\\n"
        "    defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_ANDROID) ||            \\\n"
        "    defined(DART_HOST_OS_HORIZON)",
        "dart platform/synchronization.h",
    )


def patch_dart_signal_handler(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "vm",
                        "signal_handler.h")
    # Horizon kennt weder ucontext_t noch mcontext_t; einen signalbasierten
    # Profiler gibt es dort nicht. Windows loest dasselbe Problem mit reinen
    # Deklarationen - wir folgen dem. siginfo_t und sigset_t liefert newlib.
    replace_once(
        path,
        "#elif defined(DART_HOST_OS_FUCHSIA)\n"
        "#include <signal.h>    // NOLINT\n"
        "#include <ucontext.h>  // NOLINT\n"
        "#endif",
        "#elif defined(DART_HOST_OS_FUCHSIA)\n"
        "#include <signal.h>    // NOLINT\n"
        "#include <ucontext.h>  // NOLINT\n"
        "#elif defined(DART_HOST_OS_HORIZON)\n"
        "// Horizon hat kein ucontext_t/mcontext_t und keinen signalbasierten\n"
        "// Profiler. Wie unter Windows reicht die Deklaration, damit die\n"
        "// Schnittstelle uebersetzt.\n"
        "#include <signal.h>  // NOLINT\n"
        "struct mcontext_t;\n"
        "#endif",
        "dart vm/signal_handler.h",
    )


def patch_dart_version_string(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "vm",
                        "dart.cc")
    # Reiner Anzeigetext in Dart::VersionString().
    replace_once(
        path,
        "#elif defined(DART_TARGET_OS_WINDOWS)\n"
        '    buffer.AddString(" windows");\n'
        "#else\n"
        "#error What operating system?",
        "#elif defined(DART_TARGET_OS_WINDOWS)\n"
        '    buffer.AddString(" windows");\n'
        "#elif defined(DART_TARGET_OS_HORIZON)\n"
        '    buffer.AddString(" horizon");\n'
        "#else\n"
        "#error What operating system?",
        "dart vm/dart.cc (VersionString)",
    )


def patch_dart_vm_sources(src: str, repo: str) -> None:
    """Legt die Horizon-Implementierungen ab und traegt sie in die Quellenliste ein.

    Dart listet alle Plattformdateien flach in vm_sources.gni und laesst jede
    ihren Inhalt selbst hinter #if defined(DART_HOST_OS_...) verbergen. Unsere
    Dateien folgen demselben Muster und sind auf anderen Plattformen leer.
    """
    import shutil

    runtime = os.path.join(src, "flutter", "third_party", "dart", "runtime")
    files = os.path.join(repo, "patches", "flutter-engine", "files", "dart", "vm")

    names = (
        "os_horizon.cc",
        "os_thread_horizon.cc",
        "virtual_memory_horizon.cc",
        "cpuinfo_horizon.cc",
        "native_symbol_horizon.cc",
    )
    for name in names:
        shutil.copyfile(os.path.join(files, name),
                        os.path.join(runtime, "vm", name))
        print(f"    kopiert: dart/runtime/vm/{name}")

    gni = os.path.join(runtime, "vm", "vm_sources.gni")
    for new, anchor in (
        ("os_horizon.cc", "os_linux.cc"),
        ("os_thread_horizon.cc", "os_thread_linux.cc"),
        ("virtual_memory_horizon.cc", "virtual_memory_posix.cc"),
        ("cpuinfo_horizon.cc", "cpuinfo_linux.cc"),
        ("native_symbol_horizon.cc", "native_symbol_posix.cc"),
    ):
        replace_once(
            gni,
            f'  "{anchor}",\n',
            f'  "{new}",\n  "{anchor}",\n',
            f"vm_sources.gni ({new})",
        )


def patch_dart_vm_libs(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "vm",
                        "BUILD.gn")
    # Horizon hat kein libdl - dynamisches Nachladen gibt es dort nicht. pthread
    # und atomic stecken bereits in newlib bzw. libgcc; ein eigenes -lpthread
    # gaebe es gar nicht zu linken.
    replace_once(
        path,
        "  } else {\n"
        '    libs = [ "dl" ]\n'
        "    if (!is_android) {\n"
        '      libs += [ "pthread" ]\n'
        "    }",
        "  } else if (is_horizon) {\n"
        "    # Kein libdl: Horizon kennt kein dlopen. pthread und atomic sind\n"
        "    # Teil von newlib bzw. libgcc und brauchen keinen eigenen Eintrag.\n"
        "    libs = []\n"
        "  } else {\n"
        '    libs = [ "dl" ]\n'
        "    if (!is_android) {\n"
        '      libs += [ "pthread" ]\n'
        "    }",
        "dart vm/BUILD.gn (libs)",
    )


def patch_native_assets(src: str) -> None:
    path = os.path.join(src, "flutter", "assets", "native_assets.cc")
    # Der Name bildet zusammen mit der Architektur den Schluessel im
    # native_assets-Manifest ("horizon_arm64"). Native Assets sind auf Horizon
    # ohnehin nicht ladbar - es gibt kein dlopen -, aber der Code muss
    # uebersetzen.
    replace_once(
        path,
        '#elif defined(FML_OS_WIN)\n'
        '#define kTargetOperatingSystemName "windows"\n'
        "#else\n"
        "#error Target operating system detection failed.",
        '#elif defined(FML_OS_WIN)\n'
        '#define kTargetOperatingSystemName "windows"\n'
        "#elif defined(FML_OS_HORIZON)\n"
        '#define kTargetOperatingSystemName "horizon"\n'
        "#else\n"
        "#error Target operating system detection failed.",
        "flutter/assets/native_assets.cc",
    )


# dart:io – Dateien, die auf Horizon dasselbe tun wie unter Linux.
#
# Statt Kopien anzulegen wird der vorhandene Waechter erweitert. Das haelt die
# Abweichung vom Upstream klein und macht sichtbar, wo Horizon sich wirklich
# unterscheidet: naemlich nur dort, wo eine eigene Datei entsteht.
#
# Welche Datei hier hineingehoert, entscheidet der Compiler. Der Ansatz ist
# bewusst empirisch: Waechter erweitern, bauen, und was scheitert, bekommt eine
# eigene Fassung.
DART_BIN_POSIX_REUSE = (
    "thread_linux.cc",
    "utils_linux.cc",
    "fdutils_linux.cc",
    "socket_base_linux.cc",
    "socket_linux.cc",
    "sync_socket_linux.cc",
    # stdio_linux.cc fehlt hier bewusst: Die Datei besteht fast vollstaendig
    # aus Terminalsteuerung ueber termios, die es auf Horizon nicht gibt.
    # Dafuer existiert stdio_horizon.cc.
    "file_linux.cc",
    "directory_linux.cc",
    "platform_linux.cc",
)


def patch_dart_bin_guards(src: str) -> None:
    base = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin")
    old = "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID)"
    new = ("#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID) ||     \\\n"
           "    defined(DART_HOST_OS_HORIZON)")

    # thread_linux.cc hat einen mehrzeiligen Waechter mit zusaetzlicher
    # Bedingung und braucht deshalb eine eigene Behandlung.
    old_multiline = ("#if (defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID)) &&          \\\n")
    new_multiline = ("#if (defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_ANDROID) ||           \\\n"
                     "     defined(DART_HOST_OS_HORIZON)) &&                                         \\\n")

    for name in DART_BIN_POSIX_REUSE:
        path = os.path.join(base, name)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if "DART_HOST_OS_HORIZON" in text:
            print(f"    schon erweitert: bin/{name}")
            continue
        if old_multiline in text:
            text = text.replace(old_multiline, new_multiline, 1)
        elif old in text:
            text = text.replace(old, new, 1)
        else:
            print(f"    WAECHTER NICHT ERKANNT: bin/{name}", file=sys.stderr)
            continue
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        print(f"    erweitert: bin/{name}")


def patch_dart_bin_headers(src: str, repo: str) -> None:
    import shutil

    base = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin")
    files = os.path.join(repo, "patches", "flutter-engine", "files", "dart", "bin")

    for name in ("eventhandler_horizon.h", "eventhandler_horizon.cc",
                 "socket_base_horizon.h", "stdio_horizon.cc",
                 "posix_at_horizon.cc"):
        shutil.copyfile(os.path.join(files, name), os.path.join(base, name))
        print(f"    kopiert: dart/runtime/bin/{name}")

    replace_once(
        os.path.join(base, "io_impl_sources.gni"),
        '  "eventhandler_linux.cc",\n',
        '  "eventhandler_horizon.cc",\n  "eventhandler_horizon.h",\n'
        '  "eventhandler_linux.cc",\n',
        "dart bin/io_impl_sources.gni",
    )

    replace_once(
        os.path.join(base, "io_impl_sources.gni"),
        '  "stdio_linux.cc",\n',
        '  "stdio_horizon.cc",\n  "posix_at_horizon.cc",\n  "stdio_linux.cc",\n',
        "dart bin/io_impl_sources.gni (stdio)",
    )


    # socket_base_linux.h bindet <sys/un.h> ein, das libnx nicht hat - deshalb
    # eine eigene, ansonsten gleiche Fassung.
    replace_once(
        os.path.join(base, "socket_base.h"),
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "bin/socket_base_win.h"\n'
        "#else\n"
        "#error Unknown target os.",
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "bin/socket_base_win.h"\n'
        "#elif defined(DART_HOST_OS_HORIZON)\n"
        '#include "bin/socket_base_horizon.h"\n'
        "#else\n"
        "#error Unknown target os.",
        "dart bin/socket_base.h",
    )

    # eventhandler_linux.h setzt epoll voraus, das Horizon nicht hat - hier
    # fuehrt kein Weg an einer eigenen Fassung vorbei.
    replace_once(
        os.path.join(base, "eventhandler.h"),
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "bin/eventhandler_win.h"\n'
        "#else\n"
        "#error Unknown target os.",
        '#elif defined(DART_HOST_OS_WINDOWS)\n'
        '#include "bin/eventhandler_win.h"\n'
        "#elif defined(DART_HOST_OS_HORIZON)\n"
        '#include "bin/eventhandler_horizon.h"\n'
        "#else\n"
        "#error Unknown target os.",
        "dart bin/eventhandler.h",
    )


def patch_dart_bin_platform(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin",
                        "platform_linux.cc")

    replace_once(
        path,
        "#include <sys/prctl.h>\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <sys/prctl.h>  // NOLINT\n"
        "#endif\n",
        "dart bin/platform_linux.cc (prctl-Include)",
    )

    # Horizon kennt weder prctl noch einen Prozessnamen, den man setzen
    # koennte. Die Angabe dient unter Linux nur Werkzeugen wie top.
    replace_once(
        path,
        "void Platform::SetProcessName(const char* name) {\n"
        "  prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(name), 0, 0, 0);  // NOLINT\n"
        "}",
        "void Platform::SetProcessName(const char* name) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Horizon fuehrt keinen setzbaren Prozessnamen.\n"
        "  (void)name;\n"
        "#else\n"
        "  prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(name), 0, 0, 0);  // NOLINT\n"
        "#endif\n"
        "}",
        "dart bin/platform_linux.cc (SetProcessName)",
    )

    # Der gesamte Absturz-Handler entfaellt auf Horizon. Speicherfehler landen
    # dort nicht in einem POSIX-Signal, sondern in der Ausnahmebehandlung des
    # Systems; sigaction mit SA_SIGINFO, sa_sigaction und setrlimit gibt es in
    # newlib ohnehin nicht. Platform::Initialize meldet deshalb schlicht Erfolg.
    replace_once(
        path,
        "bool Platform::Initialize() {\n"
        "  // Turn off the signal handler for SIGPIPE",
        "bool Platform::Initialize() {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Horizon liefert keine POSIX-Signale fuer Speicherfehler, und\n"
        "  // newlib kennt weder SA_SIGINFO noch setrlimit. Die Absturzausgabe\n"
        "  // von Dart erscheint auf dieser Plattform folglich nie - die\n"
        "  // Diagnose laeuft ueber die Logsenke des Embedders.\n"
        "  return true;\n"
        "#else\n"
        "  // Turn off the signal handler for SIGPIPE",
        "dart bin/platform_linux.cc (Initialize Anfang)",
    )

    replace_once(
        path,
        "    perror(\"sigaction() failed.\");\n"
        "    return false;\n"
        "  }\n"
        "  return true;\n"
        "}\n"
        "\n"
        "int Platform::NumberOfProcessors() {",
        "    perror(\"sigaction() failed.\");\n"
        "    return false;\n"
        "  }\n"
        "  return true;\n"
        "#endif  // defined(DART_HOST_OS_HORIZON)\n"
        "}\n"
        "\n"
        "int Platform::NumberOfProcessors() {",
        "dart bin/platform_linux.cc (Initialize Ende)",
    )

    # Core-Dumps gibt es auf Horizon nicht, und damit auch keine Grenze dafuer.
    replace_once(
        path,
        "void Platform::SetCoreDumpResourceLimit(int value) {\n"
        "  rlimit limit = {static_cast<rlim_t>(value), static_cast<rlim_t>(value)};\n"
        "  setrlimit(RLIMIT_CORE, &limit);\n"
        "}",
        "void Platform::SetCoreDumpResourceLimit(int value) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Kein Core-Dump, keine Grenze. Absturzabbilder liefert auf Horizon\n"
        "  // allenfalls das System selbst.\n"
        "  (void)value;\n"
        "#else\n"
        "  rlimit limit = {static_cast<rlim_t>(value), static_cast<rlim_t>(value)};\n"
        "  setrlimit(RLIMIT_CORE, &limit);\n"
        "#endif\n"
        "}",
        "dart bin/platform_linux.cc (SetCoreDumpResourceLimit)",
    )

    # Der Handler selbst benutzt siginfo_t::si_addr, das newlib nicht kennt.
    replace_once(
        path,
        "static void segv_handler(int signal, siginfo_t* siginfo, void* context) {",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "static void segv_handler(int signal, siginfo_t* siginfo, void* context) {",
        "dart bin/platform_linux.cc (segv_handler Anfang)",
    )

    replace_once(
        path,
        "  Dart_DumpNativeStackTrace(context);\n"
        "  Dart_PrepareToAbort();\n"
        "  abort();\n"
        "}",
        "  Dart_DumpNativeStackTrace(context);\n"
        "  Dart_PrepareToAbort();\n"
        "  abort();\n"
        "}\n"
        "#endif  // !defined(DART_HOST_OS_HORIZON)",
        "dart bin/platform_linux.cc (segv_handler Ende)",
    )

    # strcode() entschluesselt si_code-Konstanten fuer den Absturz-Handler.
    # newlib definiert sie nicht, und Horizon liefert ohnehin keine
    # POSIX-Signale fuer Speicherfehler - dort greift die eigene
    # Ausnahmebehandlung des Systems.
    replace_once(
        path,
        "static const char* strcode(int si_signo, int si_code) {\n"
        "#define CASE(signo, code)",
        "static const char* strcode(int si_signo, int si_code) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  (void)si_signo;\n"
        "  (void)si_code;\n"
        "#else\n"
        "#define CASE(signo, code)",
        "dart bin/platform_linux.cc (strcode Anfang)",
    )

    replace_once(
        path,
        "  CASE(SIGTRAP, TRAP_TRACE);\n"
        "#undef CASE\n"
        '  return "?";',
        "  CASE(SIGTRAP, TRAP_TRACE);\n"
        "#undef CASE\n"
        "#endif  // defined(DART_HOST_OS_HORIZON)\n"
        '  return "?";',
        "dart bin/platform_linux.cc (strcode Ende)",
    )

    replace_once(
        path,
        "#include <sys/resource.h>\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <sys/resource.h>\n"
        "#endif\n",
        "dart bin/platform_linux.cc (sys/resource.h)",
    )

    replace_once(
        path,
        "#include <sys/utsname.h>\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <sys/utsname.h>  // uname; gibt es auf Horizon nicht\n"
        "#endif\n",
        "dart bin/platform_linux.cc (sys/utsname.h)",
    )

    # Die Switch hat vier Kerne, von denen Homebrew regulaer drei benutzen darf;
    # der vierte ist dem System vorbehalten. sysconf kennt newlib nicht.
    replace_once(
        path,
        "int Platform::NumberOfProcessors() {\n"
        "  return sysconf(_SC_NPROCESSORS_ONLN);\n"
        "}",
        "int Platform::NumberOfProcessors() {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Vier Kerne, davon drei fuer Homebrew nutzbar.\n"
        "  return 3;\n"
        "#else\n"
        "  return sysconf(_SC_NPROCESSORS_ONLN);\n"
        "#endif\n"
        "}",
        "dart bin/platform_linux.cc (NumberOfProcessors)",
    )

    # uname gibt es nicht. Die Firmwareversion waere ueber libnx zu haben,
    # zoege aber <switch.h> in die Dart-VM. nullptr heisst "unbekannt".
    replace_once(
        path,
        "const char* Platform::OperatingSystemVersion() {\n"
        "#if defined(DART_HOST_OS_ANDROID)\n",
        "const char* Platform::OperatingSystemVersion() {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  return nullptr;\n"
        "#elif defined(DART_HOST_OS_ANDROID)\n",
        "dart bin/platform_linux.cc (OperatingSystemVersion)",
    )

    replace_once(
        path,
        "const char* Platform::ResolveExecutablePath() {\n"
        '  return File::ReadLink("/proc/self/exe");\n'
        "}",
        "const char* Platform::ResolveExecutablePath() {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  return nullptr;  // kein /proc\n"
        "#else\n"
        '  return File::ReadLink("/proc/self/exe");\n'
        "#endif\n"
        "}",
        "dart bin/platform_linux.cc (ResolveExecutablePath)",
    )

    # Es gibt kein /proc. Der Rueckgabewert -1 bedeutet "unbekannt"; Dart
    # meldet dann keinen aufgeloesten Pfad, statt einen erfundenen zu liefern.
    replace_once(
        path,
        "intptr_t Platform::ResolveExecutablePathInto(char* result, size_t result_size) {\n"
        '  return File::ReadLinkInto("/proc/self/exe", result, result_size);\n'
        "}",
        "intptr_t Platform::ResolveExecutablePathInto(char* result, size_t result_size) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Kein /proc: Der eigene Pfad ist ueber diesen Weg nicht zu\n"
        "  // ermitteln. -1 heisst unbekannt.\n"
        "  return -1;\n"
        "#else\n"
        '  return File::ReadLinkInto("/proc/self/exe", result, result_size);\n'
        "#endif\n"
        "}",
        "dart bin/platform_linux.cc (ResolveExecutablePathInto)",
    )


def patch_dart_bin_socket_base(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin",
                        "socket_base_linux.cc")
    # ifaddrs.h wird eingebunden, aber in dieser Datei nirgends benutzt - wie
    # zuvor schon sys/mman.h in fml/platform/posix/file_posix.cc. Fuer Horizon
    # ausgeklammert statt entfernt, weil andere Plattformen ihn ueber diesen
    # Weg transitiv beziehen koennten.
    replace_once(
        path,
        "#include <ifaddrs.h>      // NOLINT\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <ifaddrs.h>      // NOLINT\n"
        "#endif\n",
        "dart bin/socket_base_linux.cc (ifaddrs.h)",
    )

    # Die Datei bindet ihren Plattformheader am Ende direkt ein. Fuer Horizon
    # muss das der eigene sein, sonst kommt <sys/un.h> ueber die Hintertuer
    # wieder herein.
    replace_once(
        path,
        '#include "bin/socket_base_linux.h"\n',
        "#if defined(DART_HOST_OS_HORIZON)\n"
        '#include "bin/socket_base_horizon.h"\n'
        "#else\n"
        '#include "bin/socket_base_linux.h"\n'
        "#endif\n",
        "dart bin/socket_base_linux.cc (Plattformheader)",
    )


def patch_dart_bin_socket(src: str) -> None:
    base = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin")

    # stat64/fstat64 sind die Large-File-Varianten von glibc. newlib fuehrt
    # off_t ohnehin 64-bittig und kennt nur stat/fstat.
    replace_once(
        os.path.join(base, "socket_base_linux.cc"),
        "int SocketBase::GetType(intptr_t fd) {\n"
        "  struct stat64 buf;\n"
        "  int result = TEMP_FAILURE_RETRY(fstat64(fd, &buf));",
        "int SocketBase::GetType(intptr_t fd) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  struct stat buf;\n"
        "  int result = TEMP_FAILURE_RETRY(fstat(fd, &buf));\n"
        "#else\n"
        "  struct stat64 buf;\n"
        "  int result = TEMP_FAILURE_RETRY(fstat64(fd, &buf));\n"
        "#endif",
        "dart bin/socket_base_linux.cc (fstat64)",
    )

    socket_linux = os.path.join(base, "socket_linux.cc")

    # ENONET ("Machine is not on the network") ist eine Linux-Eigenheit.
    replace_once(
        socket_linux,
        "         (error == ENOPROTOOPT) || (error == EHOSTDOWN) || (error == ENONET) ||",
        "         (error == ENOPROTOOPT) || (error == EHOSTDOWN) ||\n"
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "         (error == ENONET) ||\n"
        "#endif",
        "dart bin/socket_linux.cc (ENONET)",
    )

    # accept4 setzt Flags gleich beim Annehmen; newlib kennt nur accept.
    # Dieselben Eigenschaften lassen sich danach ueber fcntl setzen - genau
    # das tat auch Linux, bevor accept4 hinzukam.
    replace_once(
        socket_linux,
        "  socket = TEMP_FAILURE_RETRY(accept4(fd, &client_addr.addr, &client_addr.size,\n"
        "                                      SOCK_NONBLOCK | SOCK_CLOEXEC));",
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  socket = TEMP_FAILURE_RETRY(\n"
        "      accept(fd, &client_addr.addr, &client_addr.size));\n"
        "  if (socket != -1) {\n"
        "    // accept4 haette das in einem Schritt erledigt; ohne es bleibt der\n"
        "    // Weg ueber fcntl. Close-on-exec ist auf Horizon gegenstandslos,\n"
        "    // weil es kein exec gibt.\n"
        "    if (!FDUtils::SetNonBlocking(socket)) {\n"
        "      close(socket);\n"
        "      socket = -1;\n"
        "    }\n"
        "  }\n"
        "#else\n"
        "  socket = TEMP_FAILURE_RETRY(accept4(fd, &client_addr.addr, &client_addr.size,\n"
        "                                      SOCK_NONBLOCK | SOCK_CLOEXEC));\n"
        "#endif",
        "dart bin/socket_linux.cc (accept4)",
    )


def patch_dart_bin_file(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin",
                        "file_linux.cc")

    replace_once(
        path,
        "#include <sys/mman.h>      // NOLINT\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <sys/mman.h>      // NOLINT\n"
        "#endif\n",
        "dart bin/file_linux.cc (sys/mman.h)",
    )

    replace_once(
        path,
        "#include <sys/sendfile.h>  // NOLINT\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <sys/sendfile.h>  // NOLINT\n"
        "#endif\n",
        "dart bin/file_linux.cc (sys/sendfile.h)",
    )

    # sendfile kopiert im Kernel und spart den Umweg ueber den Benutzerspeicher.
    # Horizon hat es nicht - aber die Datei bringt fuer genau diesen Fall schon
    # einen Rueckfallweg ueber read/write mit, den sie bei ENOSYS einschlaegt.
    # Wir gehen direkt dorthin.
    replace_once(
        path,
        "  int64_t offset = 0;\n"
        "  intptr_t result = 1;\n"
        "  while (result > 0) {\n"
        "    // Loop to ensure we copy everything, and not only up to 2GB.\n"
        "    result = NO_RETRY_EXPECTED(sendfile64(new_fd, old_fd, &offset, kMaxUint32));\n"
        "  }\n",
        "  int64_t offset = 0;\n"
        "  intptr_t result = 1;\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Kein sendfile. Der vorhandene Rueckfallweg unten erledigt es ueber\n"
        "  // read/write; ENOSYS ist genau die Bedingung, auf die er wartet.\n"
        "  result = -1;\n"
        "  errno = ENOSYS;\n"
        "#else\n"
        "  while (result > 0) {\n"
        "    // Loop to ensure we copy everything, and not only up to 2GB.\n"
        "    result = NO_RETRY_EXPECTED(sendfile64(new_fd, old_fd, &offset, kMaxUint32));\n"
        "  }\n"
        "#endif\n",
        "dart bin/file_linux.cc (sendfile)",
    )

    # Vierte Stelle mit demselben Thema nach fml, Dart-VM und Skia: Ohne mmap
    # wird gelesen statt abgebildet.
    #
    # Anders als dort sind hier zwei Faelle nicht erfuellbar:
    #   * kReadExecute braucht ausfuehrbaren Speicher. Den gibt es nicht, und
    #     der AOT-Weg dieses Projekts braucht ihn auch nicht - die
    #     Snapshot-Instruktionen liegen in der .text der NRO.
    #   * kReadWrite verlangt, dass Aenderungen in die Datei zurueckfliessen.
    #     Ein Heap-Puffer kann das nicht.
    # Beide melden deshalb nullptr statt eine abweichende Zusicherung.
    replace_once(
        path,
        "  void* hint = nullptr;\n"
        "  int prot = PROT_NONE;\n",
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  if ((type != kReadOnly) || (start != nullptr)) {\n"
        "    // Nur lesende Abbildungen ohne feste Zieladresse sind nachbildbar.\n"
        "    return nullptr;\n"
        "  }\n"
        "  void* addr = malloc(length);\n"
        "  if (addr == nullptr) {\n"
        "    return nullptr;\n"
        "  }\n"
        "  int64_t total = 0;\n"
        "  while (total < length) {\n"
        "    const ssize_t bytes = TEMP_FAILURE_RETRY(\n"
        "        pread(handle_->fd(), static_cast<char*>(addr) + total,\n"
        "              length - total, position + total));\n"
        "    if (bytes <= 0) {\n"
        "      free(addr);\n"
        "      return nullptr;\n"
        "    }\n"
        "    total += bytes;\n"
        "  }\n"
        "  return new MappedMemory(addr, length, /*should_unmap=*/true);\n"
        "#else\n"
        "  void* hint = nullptr;\n"
        "  int prot = PROT_NONE;\n",
        "dart bin/file_linux.cc (File::Map)",
    )

    replace_once(
        path,
        "  return new MappedMemory(addr, length, /*should_unmap=*/start == nullptr);\n"
        "}\n"
        "\n"
        "void MappedMemory::Unmap() {\n"
        "  int result = munmap(address_, size_);\n"
        "  ASSERT(result == 0);\n",
        "  return new MappedMemory(addr, length, /*should_unmap=*/start == nullptr);\n"
        "#endif  // defined(DART_HOST_OS_HORIZON)\n"
        "}\n"
        "\n"
        "void MappedMemory::Unmap() {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  free(address_);\n"
        "#else\n"
        "  int result = munmap(address_, size_);\n"
        "  ASSERT(result == 0);\n"
        "#endif\n",
        "dart bin/file_linux.cc (MappedMemory::Unmap)",
    )


def patch_dart_bin_thread(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin",
                        "thread_linux.cc")
    # pthread_setname_np ist eine GNU-Erweiterung. Der Name dient unter Linux
    # Werkzeugen wie top und gdb; auf Horizon gibt es weder das eine noch das
    # andere, und newlib kennt die Funktion nicht.
    replace_once(
        path,
        "  char truncated_name[16];\n"
        '  snprintf(truncated_name, sizeof(truncated_name), "%s", name);\n'
        "  pthread_setname_np(pthread_self(), truncated_name);\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "  char truncated_name[16];\n"
        '  snprintf(truncated_name, sizeof(truncated_name), "%s", name);\n'
        "  pthread_setname_np(pthread_self(), truncated_name);\n"
        "#else\n"
        "  // Kein pthread_setname_np in newlib, und kein Werkzeug, das den\n"
        "  // Namen anzeigen wuerde.\n"
        "  (void)name;\n"
        "#endif\n",
        "dart bin/thread_linux.cc (pthread_setname_np)",
    )


def patch_dart_bin_native_assets(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin",
                        "native_assets_api_impl.cc")
    # Ohne Laufzeit-Linker gibt es keinen Prozess-Handle, in dem sich Symbole
    # nachschlagen liessen. Native Assets funktionieren auf Horizon
    # grundsaetzlich nicht - siehe docs/target-apps.md.
    replace_once(
        path,
        "void* NativeAssets::DlopenProcess(char** error) {\n"
        "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_MACOS) ||              \\\n",
        "void* NativeAssets::DlopenProcess(char** error) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Kein Laufzeit-Linker, also kein Prozess-Handle.\n"
        "  return nullptr;\n"
        "#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_MACOS) ||            \\\n",
        "dart bin/native_assets_api_impl.cc (DlopenProcess)",
    )


def patch_dart_platform_utils(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime",
                        "platform", "utils.cc")
    # basename() steht in <libgen.h>, das devkitA64 mitbringt. Die Datei bindet
    # es nur im Linux-Zweig ein - dlfcn.h dagegen gibt es hier nicht.
    replace_once(
        path,
        "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_MACOS) ||              \\\n"
        "    defined(DART_HOST_OS_ANDROID)\n"
        "#include <dlfcn.h>\n"
        "#include <libgen.h>\n",
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "// Kein dlfcn.h auf Horizon; libgen.h liefert basename.\n"
        "#include <libgen.h>\n"
        "#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_MACOS) ||            \\\n"
        "    defined(DART_HOST_OS_ANDROID)\n"
        "#include <dlfcn.h>\n"
        "#include <libgen.h>\n",
        "dart platform/utils.cc (libgen.h)",
    )


def patch_dart_ffi_dynamic_library(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime", "lib",
                        "ffi_dynamic_library.cc")
    # Dieselbe Lage wie bei den native assets: Ohne Laufzeit-Linker gibt es
    # keinen Prozess-Handle. Dart.dl.processLibrary liefert damit einen
    # nullptr-Handle; jede Symbolsuche darauf schlaegt fehl, was die ehrliche
    # Antwort ist.
    replace_once(
        path,
        "DEFINE_NATIVE_ENTRY(Ffi_dl_processLibrary, 0, 0) {\n"
        "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_MACOS) ||              \\\n",
        "DEFINE_NATIVE_ENTRY(Ffi_dl_processLibrary, 0, 0) {\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Kein Laufzeit-Linker, also kein Prozess-Handle.\n"
        "  return DynamicLibrary::New(nullptr, false);\n"
        "#elif defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_MACOS) ||            \\\n",
        "dart lib/ffi_dynamic_library.cc (Ffi_dl_processLibrary)",
    )

    # Zweite Stelle: Symbolsuche im eigenen Prozess. Ohne Laufzeit-Linker
    # nicht moeglich; nullptr plus gesetzte Fehlermeldung ist der Weg, den
    # der Aufrufer ohnehin behandelt.
    replace_once(
        path,
        "  // Resolution in current process.\n"
        "#if !defined(DART_HOST_OS_WINDOWS)\n"
        "  void* const result = Utils::ResolveSymbolInDynamicLibrary(\n"
        "      RTLD_DEFAULT, symbol.ToCString(), error);\n",
        "  // Resolution in current process.\n"
        "#if defined(DART_HOST_OS_HORIZON)\n"
        "  // Symbole lassen sich zur Laufzeit nicht ueber Namen aufloesen.\n"
        "  void* const result = nullptr;\n"
        "  *error = OS::SCreate(/*use malloc*/ nullptr,\n"
        "                       \"Symbol lookup by name is not available on \"\n"
        "                       \"Horizon: there is no runtime linker \"\n"
        "                       \"(symbol '%s').\",\n"
        "                       symbol.ToCString());\n"
        "#elif !defined(DART_HOST_OS_WINDOWS)\n"
        "  void* const result = Utils::ResolveSymbolInDynamicLibrary(\n"
        "      RTLD_DEFAULT, symbol.ToCString(), error);\n",
        "dart lib/ffi_dynamic_library.cc (Symbolsuche im Prozess)",
    )


def patch_tonic_build_config(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "tonic", "common",
                        "build_config.h")
    # Dasselbe Muster wie in fml/build_config.h.
    replace_once(
        path,
        "#elif defined(__QNXNTO__)\n"
        "#define OS_QNX 1\n"
        "#else\n"
        "#error Please add support for your platform in tonic/common/build_config.h",
        "#elif defined(__QNXNTO__)\n"
        "#define OS_QNX 1\n"
        "#elif defined(__SWITCH__)\n"
        "#define OS_HORIZON 1\n"
        "#else\n"
        "#error Please add support for your platform in tonic/common/build_config.h",
        "tonic/common/build_config.h",
    )


def patch_absl_cctz(src: str) -> None:
    path = os.path.join(src, "third_party", "abseil-cpp", "absl", "time",
                        "internal", "cctz", "src", "time_zone_libc.cc")
    # newlib fuehrt keine tm_gmtoff/tm_zone in struct tm, wohl aber die
    # Globalen _timezone und _tzname (time.h:139-147). Das ist genau die
    # Ausgangslage des NaCl-Zweigs, der ebenfalls fuer schlanke Plattformen
    # gedacht ist - Horizon reiht sich dort ein.
    replace_once(
        path,
        "#elif defined(__native_client__) || defined(__myriad2__) || \\\n"
        "    defined(__EMSCRIPTEN__)",
        "#elif defined(__native_client__) || defined(__myriad2__) || \\\n"
        "    defined(__EMSCRIPTEN__) || defined(__SWITCH__)",
        "abseil cctz time_zone_libc.cc",
    )


def patch_implicit_float_conversion(src: str) -> None:
    """-Wimplicit-float-conversion kennt nur Clang.

    GCC hat kein Gegenstueck: -Wfloat-conversion ist naeher dran, meldet aber
    auch Verengungen zwischen Gleitkommatypen, die hier nicht gemeint sind.
    Die Warnung entfaellt fuer Horizon ersatzlos - sie dient dazu, unbemerkte
    double-nach-float-Verengungen an der Dart-Grenze zu finden, und diese
    Pruefung leistet der Upstream-Build auf den anderen Plattformen weiterhin.
    """
    for rel in ("flutter/lib/ui/BUILD.gn", "flutter/lib/gpu/BUILD.gn"):
        replace_once(
            os.path.join(src, rel),
            '    "-Wimplicit-float-conversion",\n'
            "  ]\n",
            "  ]\n"
            "  if (is_clang) {\n"
            "    # Nur Clang kennt diese Warnung.\n"
            '    cflags += [ "-Wimplicit-float-conversion" ]\n'
            "  }\n",
            f"{rel} (-Wimplicit-float-conversion)",
        )


def patch_embedder_surface(src: str) -> None:
    path = os.path.join(src, "flutter", "shell", "platform", "embedder",
                        "embedder_surface.cc")
    # CreateResourceContext gibt sk_sp<GrDirectContext> zurueck. Fuer den
    # Destruktor des Smart Pointers braucht der Compiler den vollstaendigen
    # Typ. Auf Plattformen mit GPU-Backend kommt der Header transitiv ueber
    # die GL-Header mit - ohne GL fehlt er.
    replace_once(
        path,
        '#include "flutter/shell/platform/embedder/embedder_surface.h"\n',
        '#include "flutter/shell/platform/embedder/embedder_surface.h"\n'
        "\n"
        '#include "third_party/skia/include/gpu/ganesh/GrDirectContext.h"\n',
        "flutter/shell/platform/embedder/embedder_surface.cc (GrDirectContext)",
    )


def patch_remaining_libdl(src: str) -> None:
    """Die letzten Stellen, die -ldl anhaengen. libnx hat kein libdl."""
    replace_once(
        os.path.join(src, "flutter", "skia", "BUILD.gn"),
        "    if (!is_qnx) {\n"
        '      libs += [ "dl" ]\n'
        "    }",
        "    if (!is_qnx && !is_horizon) {\n"
        '      libs += [ "dl" ]\n'
        "    }",
        "flutter/skia/BUILD.gn (libdl)",
    )

    replace_once(
        os.path.join(src, "flutter", "third_party", "dart", "runtime", "bin",
                     "BUILD.gn"),
        "config(\"libdart_builtin_config\") {\n"
        "  if (is_win) {\n"
        '    libs = [ "bcrypt.lib" ]\n'
        "  } else {\n"
        '    libs = [ "dl" ]\n'
        "  }",
        "config(\"libdart_builtin_config\") {\n"
        "  if (is_win) {\n"
        '    libs = [ "bcrypt.lib" ]\n'
        "  } else if (is_horizon) {\n"
        "    # Kein libdl auf Horizon.\n"
        "    libs = []\n"
        "  } else {\n"
        '    libs = [ "dl" ]\n'
        "  }",
        "dart bin/BUILD.gn (libdl)",
    )


def patch_embedder_static_library(src: str) -> None:
    """Ein statisch linkbares Ziel fuer Horizon.

    flutter_engine_library ist eine shared_library. Horizon-Homebrew kennt
    keine dynamischen Bibliotheken, und devkitA64s libsysbase ist nicht mit
    -fPIC uebersetzt - der Linker bricht folgerichtig ab.

    complete_static_lib = true buendelt saemtliche Abhaengigkeiten in ein
    Archiv, sodass sich die Engine mit einem einzigen -l in die NRO linken
    laesst.
    """
    path = os.path.join(src, "flutter", "shell", "platform", "embedder",
                        "BUILD.gn")
    replace_once(
        path,
        'shared_library("flutter_engine_library") {\n',
        "# Horizon: statt einer Shared Library ein vollstaendiges Archiv, das\n"
        "# in die NRO gelinkt wird.\n"
        "static_library(\"flutter_engine_static\") {\n"
        "  complete_static_lib = true\n"
        "  output_name = \"flutter_engine\"\n"
        "  deps = [ \":embedder_as_internal_library\" ]\n"
        "  public_configs = [ \"//flutter:config\" ]\n"
        "}\n"
        "\n"
        'shared_library("flutter_engine_library") {\n',
        "flutter/shell/platform/embedder/BUILD.gn (statisches Ziel)",
    )


def patch_dart_synchronization_posix(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "dart", "runtime",
                        "platform", "synchronization_posix.cc")
    # In synchronization.h wurden nur die Typen ergaenzt (pthread_mutex_t und
    # pthread_cond_t). Die Implementierung von Mutex und ConditionVariable
    # liegt hier - und ihr Waechter kannte Horizon nicht, weshalb der Linker
    # die Symbole vermisste.
    #
    # Genau diese Sorte Luecke sieht der Compiler nicht: Jede Uebersetzungs-
    # einheit fuer sich war stimmig.
    replace_once(
        path,
        "    (defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_FUCHSIA) ||           \\\n"
        "     defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_ANDROID))",
        "    (defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_FUCHSIA) ||           \\\n"
        "     defined(DART_HOST_OS_MACOS) || defined(DART_HOST_OS_ANDROID) ||           \\\n"
        "     defined(DART_HOST_OS_HORIZON))",
        "dart platform/synchronization_posix.cc",
    )


def patch_dart_platform_linux_files(src: str) -> None:
    """utils_linux.cc und syslog_linux.cc gelten unveraendert auch fuer Horizon.

    Beide enthalten reinen POSIX-Code: vsnprintf-Huellen bzw. Ausgabe auf
    stderr. Der Linker vermisste ihre Symbole nur, weil ihr Waechter Horizon
    nicht kannte.
    """
    base = os.path.join(src, "flutter", "third_party", "dart", "runtime",
                        "platform")
    for name in ("utils_linux.cc", "syslog_linux.cc"):
        replace_once(
            os.path.join(base, name),
            "#if defined(DART_HOST_OS_LINUX)\n",
            "#if defined(DART_HOST_OS_LINUX) || defined(DART_HOST_OS_HORIZON)\n",
            f"dart platform/{name}",
        )

    # utils_linux.cc bindet sys/utsname.h ein - fuer uname, das Horizon nicht
    # hat. Der Rest der Datei sind vsnprintf-Huellen und Zeichenkettenhilfen.
    replace_once(
        os.path.join(base, "utils_linux.cc"),
        "#include <sys/utsname.h>",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "#include <sys/utsname.h>\n"
        "#endif",
        "dart platform/utils_linux.cc (sys/utsname.h)",
    )

    # Die Datei bindet ihren Plattformheader am Ende direkt ein - wie zuvor
    # schon socket_base_linux.cc. utils_linux.h braucht <endian.h>, das newlib
    # nicht hat; utils_horizon.h loest dasselbe ueber __builtin_bswap.
    replace_once(
        os.path.join(base, "utils_linux.cc"),
        '#include "platform/utils_linux.h"\n',
        "#if defined(DART_HOST_OS_HORIZON)\n"
        '#include "platform/utils_horizon.h"\n'
        "#else\n"
        '#include "platform/utils_linux.h"\n'
        "#endif\n",
        "dart platform/utils_linux.cc (Plattformheader)",
    )

    # IsWindowsSubsystemForLinux ist in utils.h nur unter DART_HOST_OS_LINUX
    # deklariert - und auf einer Spielkonsole ohnehin gegenstandslos.
    replace_once(
        os.path.join(base, "utils_linux.cc"),
        "bool Utils::IsWindowsSubsystemForLinux() {\n"
        "  struct utsname info;\n",
        "#if !defined(DART_HOST_OS_HORIZON)\n"
        "bool Utils::IsWindowsSubsystemForLinux() {\n"
        "  struct utsname info;\n",
        "dart platform/utils_linux.cc (IsWindowsSubsystemForLinux Anfang)",
    )

    replace_once(
        os.path.join(base, "utils_linux.cc"),
        '  return strstr(info.release, "icrosoft") != nullptr;\n'
        "}\n",
        '  return strstr(info.release, "icrosoft") != nullptr;\n'
        "}\n"
        "#endif  // !defined(DART_HOST_OS_HORIZON)\n",
        "dart platform/utils_linux.cc (IsWindowsSubsystemForLinux Ende)",
    )


def patch_skia_debug(src: str) -> None:
    path = os.path.join(src, "flutter", "skia", "BUILD.gn")
    # SkDebugf wird pro Plattform bereitgestellt. Die stdio-Fassung passt und
    # wird von Linux, WASM und QNX gleichermassen benutzt.
    replace_once(
        path,
        "  if (is_linux || is_wasm || is_qnx) {\n"
        '    sources += [ "$_skia_root/src/ports/SkDebug_stdio.cpp" ]\n',
        "  if (is_linux || is_wasm || is_qnx || is_horizon) {\n"
        '    sources += [ "$_skia_root/src/ports/SkDebug_stdio.cpp" ]\n',
        "flutter/skia/BUILD.gn (SkDebug_stdio)",
    )


def patch_skia_osfile(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "skia", "src", "ports",
                        "SkOSFile_posix.cpp")

    replace_once(
        path,
        "#include <sys/mman.h>\n",
        "#if !defined(__SWITCH__)\n"
        "#include <sys/mman.h>\n"
        "#endif\n",
        "skia SkOSFile_posix.cpp (Include)",
    )

    # Nur diese drei Funktionen brauchen mmap. Auf Horizon wird die Datei
    # stattdessen vollstaendig gelesen - dieselbe Entscheidung wie in
    # fml/platform/horizon/mapping_horizon.cc, mit denselben Folgen: mehr
    # Speicher, kein bedarfsweises Einlagern.
    replace_once(
        path,
        "void sk_fmunmap(const void* addr, size_t length) {\n"
        "    munmap(const_cast<void*>(addr), length);\n"
        "}\n",
        "#if defined(__SWITCH__)\n"
        "\n"
        "// Horizon kennt kein mmap. Die Datei wird gelesen statt abgebildet.\n"
        "void sk_fmunmap(const void* addr, size_t length) {\n"
        "    free(const_cast<void*>(addr));\n"
        "}\n"
        "\n"
        "void* sk_fdmmap(int fd, size_t* size) {\n"
        "    struct stat status = {};\n"
        "    if (0 != fstat(fd, &status)) {\n"
        "        return nullptr;\n"
        "    }\n"
        "    if (!S_ISREG(status.st_mode)) {\n"
        "        return nullptr;\n"
        "    }\n"
        "    if (!SkTFitsIn<size_t>(status.st_size)) {\n"
        "        return nullptr;\n"
        "    }\n"
        "    size_t fileSize = static_cast<size_t>(status.st_size);\n"
        "\n"
        "    void* addr = malloc(fileSize);\n"
        "    if (nullptr == addr) {\n"
        "        return nullptr;\n"
        "    }\n"
        "    if (lseek(fd, 0, SEEK_SET) < 0) {\n"
        "        free(addr);\n"
        "        return nullptr;\n"
        "    }\n"
        "    size_t total = 0;\n"
        "    while (total < fileSize) {\n"
        "        ssize_t bytes = read(fd, static_cast<char*>(addr) + total,\n"
        "                             fileSize - total);\n"
        "        if (bytes <= 0) {\n"
        "            free(addr);\n"
        "            return nullptr;\n"
        "        }\n"
        "        total += static_cast<size_t>(bytes);\n"
        "    }\n"
        "\n"
        "    *size = fileSize;\n"
        "    return addr;\n"
        "}\n"
        "\n"
        "#else\n"
        "\n"
        "void sk_fmunmap(const void* addr, size_t length) {\n"
        "    munmap(const_cast<void*>(addr), length);\n"
        "}\n",
        "skia SkOSFile_posix.cpp (sk_fmunmap)",
    )

    replace_once(
        path,
        "    *size = fileSize;\n"
        "    return addr;\n"
        "}\n"
        "\n"
        "int sk_fileno(FILE* f) {\n",
        "    *size = fileSize;\n"
        "    return addr;\n"
        "}\n"
        "\n"
        "#endif  // defined(__SWITCH__)\n"
        "\n"
        "int sk_fileno(FILE* f) {\n",
        "skia SkOSFile_posix.cpp (Ende des Zweigs)",
    )


def patch_skia_config(src: str) -> None:
    path = os.path.join(src, "flutter", "skia", "BUILD.gn")
    # Nach dem Vorbild des Fuchsia-Blocks daneben. Wir bauen ausschliesslich
    # den Software-Renderer, also weder GL noch Vulkan - der erste Skia-Fehler
    # kam prompt aus dem Ganesh-GL-Backend (GrAutoLocaleSetter.h braucht
    # xlocale.h).
    #
    # Fonts kommen mit der App gebuendelt, nicht vom System: Auf Horizon gibt
    # es weder fontconfig noch nutzbare Systemschriften.
    replace_once(
        path,
        "# Skia public API, generally provided by :skia.\n",
        "if (is_horizon) {\n"
        "  # Nintendo Switch: nur Software-Rendering.\n"
        "  skia_use_gl = false\n"
        "  skia_use_vulkan = false\n"
        "  skia_use_dng_sdk = false\n"
        "\n"
        "  # Schriften liefert die App mit; fontconfig gibt es nicht.\n"
        "  skia_enable_fontmgr_custom_empty = true\n"
        "  skia_enable_fontmgr_custom_directory = false\n"
        "  skia_enable_fontmgr_custom_embedded = false\n"
        "  skia_enable_fontmgr_fontconfig = false\n"
        "}\n"
        "\n"
        "# Skia public API, generally provided by :skia.\n",
        "flutter/skia/BUILD.gn (Horizon-Konfiguration)",
    )


def patch_zlib(src: str) -> None:
    path = os.path.join(src, "flutter", "third_party", "zlib", "BUILD.gn")
    # zlibs ARMv8-CRC32-Pfad braucht eine OS-spezifische Laufzeiterkennung der
    # CPU-Faehigkeiten (getauxval unter Linux, sysctl unter macOS). Auf Horizon
    # gibt es davon nichts. Statt eine Erkennung vorzutaeuschen, schalten wir
    # die Optimierung ab.
    #
    # Spaeter lohnt ein zweiter Blick: Die Switch-Hardware ist fest, ARMv8-CRC32
    # also garantiert vorhanden. Man koennte die Erkennung schlicht durch eine
    # Konstante ersetzen - das erfordert aber einen Patch am zlib-C-Code und
    # gehoert nicht ins MVP.
    replace_once(
        path,
        "use_arm_neon_optimizations = false\n"
        'if ((current_cpu == "arm" || current_cpu == "arm64") &&\n'
        "    !(is_win && !is_clang)) {",
        "use_arm_neon_optimizations = false\n"
        'if ((current_cpu == "arm" || current_cpu == "arm64") &&\n'
        "    !(is_win && !is_clang) && !is_horizon) {",
        "third_party/zlib/BUILD.gn",
    )


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

    print("==> flutter/shell/platform/BUILD.gn")
    patch_shell_platform(SRC)

    print("==> build/config/compiler/BUILD.gn")
    patch_compiler_config(SRC)
    patch_cxx_disable_modules(SRC)
    patch_werror(SRC)
    patch_cxx_std(SRC)

    print("==> flutter/third_party/dart/runtime/BUILD.gn")
    patch_dart_runtime(SRC)

    print("==> Dart-VM: platform/globals.h")
    patch_dart_globals(SRC)

    print("==> Dart-VM: plattformabhängige Header")
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    patch_dart_platform_headers(SRC, repo_root)
    patch_dart_signal_handler(SRC)
    patch_dart_version_string(SRC)

    print("==> Dart-VM: Horizon-Implementierungen")
    patch_dart_vm_sources(SRC, repo_root)
    patch_dart_vm_libs(SRC)

    print("==> dart:io – Wächter für POSIX-gleiche Dateien")
    patch_dart_bin_guards(SRC)
    patch_dart_bin_headers(SRC, repo_root)
    patch_dart_bin_platform(SRC)
    patch_dart_bin_socket_base(SRC)
    patch_dart_bin_socket(SRC)
    patch_dart_bin_file(SRC)
    patch_dart_bin_thread(SRC)
    patch_dart_bin_native_assets(SRC)
    patch_dart_platform_utils(SRC)
    patch_dart_ffi_dynamic_library(SRC)
    patch_tonic_build_config(SRC)
    patch_absl_cctz(SRC)
    patch_implicit_float_conversion(SRC)
    patch_embedder_surface(SRC)
    patch_remaining_libdl(SRC)
    patch_embedder_static_library(SRC)
    patch_dart_synchronization_posix(SRC)
    patch_dart_platform_linux_files(SRC)

    print("==> flutter/skia/BUILD.gn")
    patch_skia_config(SRC)
    patch_skia_osfile(SRC)
    patch_skia_debug(SRC)

    print("==> flutter/third_party/zlib/BUILD.gn")
    patch_zlib(SRC)

    print("==> flutter/fml/build_config.h")
    patch_fml_build_config(SRC)

    print("==> flutter/assets/native_assets.cc")
    patch_native_assets(SRC)

    print("==> fehlende Includes")
    patch_missing_includes(SRC)

    print("==> flutter/fml/backtrace.cc")
    patch_fml_backtrace(SRC)

    print("==> third_party/abseil-cpp")
    patch_absl_gettid(SRC)
    patch_absl_elf_mem_image(SRC)

    print("==> fml-Plattformquellen für Horizon")
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    patch_fml_platform_sources(SRC, repo)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
