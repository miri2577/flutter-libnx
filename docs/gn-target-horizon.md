# Entwurf: GN-Target `current_os = "horizon"`

Stand: 2026-08-09. **Entwurf, nichts davon ist gebaut oder getestet.**
Grundlage ist der QNX-Port der Engine, der als einziges nicht-Linux-POSIX-Target
zeigt, wie klein ein neues OS gehalten werden kann.

Warum überhaupt ein eigenes Target: `#define __linux__` ist ausgeschlossen. Horizon ist
kein Linux, und der Engine-Build würde dann an hunderten Stellen falsche Annahmen treffen,
die einzeln zurückgepatcht werden müssten. Der QNX-Weg kostet stattdessen ~11 Dateien.

## Betroffene Dateien (nach QNX-Vorbild)

| Datei | Änderung |
|---|---|
| `build/config/BUILDCONFIG.gn` | neuer `else if (current_os == "horizon")`-Block, der die `is_*`-Flags setzt |
| `build/toolchain/horizon/BUILD.gn` | neu: `gcc_toolchain("horizon")` für devkitA64 |
| `build/build_config.h` | `OS_HORIZON` definieren, in die `OS_POSIX`-Liste aufnehmen |
| `build/config/compiler/BUILD.gn` | Compiler-/Linker-Flags für das Target |
| `flutter/BUILD.gn`, `fml/BUILD.gn`, `skia/BUILD.gn`, `shell/platform/BUILD.gn` | Target in die jeweiligen Bedingungen aufnehmen |

## Toolchain

Der QNX-Toolchain-Block ist eine Punktlandung für devkitA64, weil beide GCC benutzen –
kein Clang, keine Sonderbehandlung:

```gn
# build/toolchain/qnx/BUILD.gn (Original)
gcc_toolchain("qnx") {
  asm = "qcc"
  cc = "qcc"
  cxx = "q++"
  readelf = "readelf"
  nm = "ntoaarch64-nm"
  ar = "ntoaarch64-ar"
  ld = "q++"
  strip = "ntoaarch64-strip"
  toolchain_cpu = "arm64"
  toolchain_os = "linux"
  is_clang = false
}
```

Entsprechung für devkitA64 (Entwurf):

```gn
gcc_toolchain("horizon") {
  asm = "aarch64-none-elf-gcc"
  cc  = "aarch64-none-elf-gcc"
  cxx = "aarch64-none-elf-g++"
  readelf = "aarch64-none-elf-readelf"
  nm      = "aarch64-none-elf-nm"
  ar      = "aarch64-none-elf-ar"
  ld      = "aarch64-none-elf-g++"
  strip   = "aarch64-none-elf-strip"
  toolchain_cpu = "arm64"
  toolchain_os = "linux"
  is_clang = false
}
```

Bemerkenswert: QNX setzt `toolchain_os = "linux"`, obwohl es kein Linux ist. Das steuert
nur die Namensgebung innerhalb von `gcc_toolchain.gni`, nicht die Zielplattform. Ob wir das
übernehmen können, ist beim ersten `gn gen` zu prüfen – nicht vorher zu raten.

## `build_config.h`

QNX hängt sich an ein compilerdefiniertes Makro:

```c
#elif defined(__QNXNTO__)
#define OS_QNX 1
```

und steht anschließend in der `OS_POSIX`-Liste (`build_config.h:74-80`).

Für Horizon wäre das Pendant `__SWITCH__`. **Wichtiger Unterschied, der noch zu prüfen
ist:** `__SWITCH__` ist kein vom Compiler vordefiniertes Makro, sondern wird per
`-D__SWITCH__` gesetzt – libnx tut das in seinem eigenen Makefile
(`third_party/libnx/nx/Makefile:35`), und die devkitPro-`switch_rules` tun es
üblicherweise auch. Da GN die Flags selbst zusammenstellt und nicht über `switch_rules`
baut, müssen wir `-D__SWITCH__` in `build/config/compiler/BUILD.gn` explizit mitgeben.

Zu verifizieren, sobald die Toolchain steht:

```bash
aarch64-none-elf-gcc -dM -E -x c /dev/null | grep -i switch
```

Ob Horizon in die `OS_POSIX`-Liste gehört, ist eine echte Entscheidung und keine
Formalie: `newlib`/libnx liefern große Teile von POSIX (Threads, File-I/O), aber kein
`mmap` und kein `dlopen`. QNX steht drin und setzt in `BUILDCONFIG.gn` trotzdem
`is_posix = false` – die beiden Schalter bedeuten also nicht dasselbe. Erwartung:
`OS_POSIX` ja, `is_posix` zunächst `false`, damit wir jede POSIX-Annahme einzeln
freischalten statt sie pauschal zu erben.

## Offene Punkte

1. Braucht `gcc_toolchain` weitere Pflichtfelder, die QNX über Defaults bezieht?
2. Welche Flags setzt devkitPro sonst noch (Architektur, `-fPIE`, Sysroot, Specs)?
   Quelle wäre `$DEVKITPRO/libnx/switch_rules`.
3. Kommt Skia mit einem GCC-Toolchain ohne Clang zurecht? Der Engine-Build ist
   ansonsten durchgängig auf Clang ausgelegt.
4. Wie werden `newlib`-Header und libnx-Includes als Sysroot eingebunden?

Punkt 3 ist der riskanteste und wird sich erst beim ersten echten Build zeigen.
