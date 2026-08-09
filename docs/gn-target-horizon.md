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

## Zweiter Weg: der vorhandene `custom_toolchain`-Mechanismus

Beim Lesen von `BUILDCONFIG.gn:553-563` ist aufgefallen, dass die Engine bereits eine
Schnittstelle für fremde Toolchains besitzt – noch vor allen OS-Abfragen:

```gn
if (custom_toolchain != "") {
  assert(custom_sysroot != "")
  assert(custom_target_triple != "")
  host_toolchain = "//build/toolchain/linux:clang_$host_cpu"
  set_default_toolchain("//build/toolchain/custom")
}
```

`build/toolchain/custom/BUILD.gn` ruft damit `${custom_toolchain}/bin/clang` bzw. `clang++`
mit `--target=${custom_target_triple}` und `--sysroot ${custom_sysroot}` auf.

**Reizvoll**, weil es ohne jeden Patch am Buildsystem auskäme: Clang ist von Haus aus ein
Cross-Compiler, könnte `aarch64-none-elf` ansteuern und das Sysroot von devkitA64
(newlib + libnx) benutzen.

**Aber es löst nur die halbe Aufgabe.** Die Toolchain-Wahl ist unabhängig von den
`is_*`-Flags, die weiterhin aus `current_os` kommen. Bliebe `target_os = "linux"`, würde
die Engine Linux-spezifischen Code übersetzen – `epoll`, `timerfd`, `/proc` – und genau
das ist die im Auftrag verbotene Abkürzung. Ein eigenes `current_os = "horizon"` brauchen
wir also so oder so.

**Einordnung:** Der Weg über `gcc_toolchain` nach QNX-Vorbild bleibt der Hauptweg, weil
devkitA64 GCC ist und die Engine mit `is_clang = false` nachweislich umgehen kann.
`custom_toolchain` ist der dokumentierte Rückfallpfad, falls sich der GCC-Weg als zu
steinig erweist – dann mit Clang gegen das devkitA64-Sysroot, aber weiterhin mit eigenem
`current_os`.

## Was der QNX-Port an Compilerflags nötig machte

Aus `build/config/compiler/BUILD.gn`, weil es zeigt, welche Art von Anpassung realistisch
zu erwarten ist:

```gn
if (is_qnx) {
  defines += [ "_XOPEN_SOURCE=700", "_QNX_SOURCE", "SKNX_NO_SIMD" ]
}
```

Bemerkenswert ist `SKNX_NO_SIMD`: ein **Skia**-Schalter, der dessen SIMD-Pfade abschaltet.
Offenbar ließ sich Skia auf QNX nicht ohne Weiteres mit SIMD übersetzen. Für Horizon ist
dasselbe zu erwarten – AArch64-NEON sollte zwar verfügbar sein, aber falls Skia dort
klemmt, ist das der erste Hebel statt stundenlanger Fehlersuche.

Weiter fällt auf: QNX ist vom Stack-Protector ausgenommen (`:118`), bekommt `-fPIC`
zusammen mit Linux und Android (`:360`), aber ausdrücklich **nicht** `-pipe` (`:363`).

## Offene Punkte

1. Braucht `gcc_toolchain` weitere Pflichtfelder, die QNX über Defaults bezieht?
2. Welche Flags setzt devkitPro sonst noch (Architektur, `-fPIE`, Sysroot, Specs)?
   Quelle wäre `$DEVKITPRO/libnx/switch_rules`.
3. Kommt Skia mit einem GCC-Toolchain ohne Clang zurecht? Der Engine-Build ist
   ansonsten durchgängig auf Clang ausgelegt.
4. Wie werden `newlib`-Header und libnx-Includes als Sysroot eingebunden?

Punkt 3 ist der riskanteste und wird sich erst beim ersten echten Build zeigen.
