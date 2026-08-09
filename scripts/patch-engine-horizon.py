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

    # Reihenfolge zaehlt: Bei einer leeren Ersetzung waere `new in text` immer
    # wahr, und der Patch wuerde faelschlich als erledigt gelten.
    if old not in text:
        if new and new in text:
            print(f"    schon gepatcht: {label}")
        else:
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


def patch_missing_includes(src: str) -> None:
    """Fehlende Standard-Includes, die unter glibc transitiv mitkamen.

    newlib zieht deutlich weniger indirekt herein als glibc. Das sind echte
    Portabilitaetsluecken im Upstream-Code, keine Horizon-Eigenheiten - sie
    faenden sich auf jeder schlanken libc.
    """
    path = os.path.join(src, "flutter", "fml", "message_loop_task_queues.cc")
    replace_once(
        path,
        '#include "flutter/fml/message_loop_task_queues.h"\n',
        '#include "flutter/fml/message_loop_task_queues.h"\n'
        "\n"
        "#include <climits>  // ULONG_MAX; unter glibc transitiv, unter newlib nicht\n",
        "flutter/fml/message_loop_task_queues.cc",
    )


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

    print("==> flutter/third_party/zlib/BUILD.gn")
    patch_zlib(SRC)

    print("==> flutter/fml/build_config.h")
    patch_fml_build_config(SRC)

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
