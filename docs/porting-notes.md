# Porting Notes

Fortlaufendes Protokoll technischer Befunde. Neueste Einträge oben.

Format je Eintrag: Befund · Beleg (Datei:Zeile oder Kommando) · Konsequenz.

---

## 2026-08-09 – Toolchain über Container statt lokaler Installation

**Befund:** Die lokale devkitPro-Installation scheiterte an einer sudo-Passwortabfrage, die
sich nicht automatisieren ließ. Das offizielle Image `devkitpro/devkita64` (2,79 GB)
enthält alles Nötige: devkitA64 r29.2-1 mit GCC 15.2.0, binutils 2.45.1, newlib 4.6.0,
libnx 4.12.0, switch-tools 1.13.1, general-tools 1.4.4.

**Konsequenz:** `scripts/dkp.ps1` hängt das Repo als `/work` in den Container und führt
darin einen Befehl aus. Kein root, keine Installation, und alle Beteiligten bauen gegen
exakt dieselben Compilerversionen – bei einer Portierung, bei der Compilerverhalten
selbst zur Fehlerquelle wird, ist das mehr wert als die Bequemlichkeit.

Der Referenz-Checkout unter `third_party/libnx` wurde von `master` auf Tag `v4.12.0`
umgestellt, damit die Header, gegen die ich prüfe, denen im Image entsprechen.

---

## 2026-08-09 – `hello_libnx` läuft auf echter Hardware

**Befund:** Erster Build ohne Compilerfehler, sauberer Rebuild ohne eine einzige Warnung
bei `-Wall`. Ergebnis: 214 KB, `NRO0`-Magic an Offset 0x10 vorhanden. Übertragung per
`nxlink` auf die Konsole, gestartet aus hbmenu im Applet-Modus. Auf dem Bildschirm
erscheint das erwartete Bild, der Balken wandert, A färbt die Fläche.

**Konsequenz:** Die gesamte Kette steht – Quelltext, Container-Toolchain, `.elf`, `.nro`,
Übertragung, Start, Framebuffer, Eingabe. Damit ist die Plattformschicht, auf der der
Embedder aufsetzt, kein Papier mehr.

**Zur Übertragung, zwei Stolpersteine:**

1. `nxlink` aus dem Container braucht `--network host`. Ohne den Schalter scheitert der
   Verbindungsaufbau („Connection to … failed") – die Docker-NAT verträgt den
   Netloader-Handshake nicht.
2. **Den Netloader-Port nicht vorher anpingen.** Ein `Test-NetConnection` auf Port 28280
   öffnet eine TCP-Verbindung, und der Netloader nimmt nur eine an – die Probe beendet
   die Sitzung, und der anschließende echte Upload scheitert. Direkt hochladen und
   `-r` für Wiederholungen nutzen.

**Applet-Modus-Eigenheit (nicht unser Code):** Beim ersten Start aus hbmenu erscheint
häufig „Error getting name length: err=11 / No more processes"; beim zweiten Versuch
klappt es. `err=11` ist `EAGAIN`, die Meldung stammt aus hbmenu selbst.

---

## 2026-08-09 – Meilenstein 2: `gn gen` bis zur Dart-VM vorgearbeitet

Jeder Fehler einzeln geklärt statt pauschal umgangen. Die Reihenfolge, in der GN sie
liefert, ist zugleich die Landkarte des Ports:

| # | Fehler | Ursache | Lösung |
|---|---|---|---|
| 1 | `Undefined identifier is_apple` (`BUILDCONFIG.gn:327`) | `current_os = "horizon"` traf keinen Zweig, also blieb jede `is_*`-Variable undefiniert | eigener Zweig plus `is_horizon = false` in den zehn vorhandenen |
| 2 | `Toolchain not set because of unknown platform` (`:678`) | keine Toolchain für das neue OS | `build/toolchain/horizon` nach QNX-Vorbild, `gcc_toolchain` mit `is_clang = false` |
| 3 | `vpython3` fehlt (exit 127) | GN ruft Hilfsskripte über depot_tools auf | depot_tools in den `PATH` des Build-Skripts |
| 4–7 | `engine_version`/`content_hash`/`skia_version`/`dart_version` fehlen | vier `assert` in `shell/version/version.gni:19-22` | Werte aus den Pins gesetzt; sie landen nur in Artefaktnamen |
| 8 | `Unknown/Unsupported platform` (`shell/platform/BUILD.gn:28`) | Gruppe `platform` kennt horizon nicht | `deps = []` wie bei QNX – der Embedder hängt separat unter `shell/platform/embedder` |
| 9 | `Unknown target_os: horizon` (`third_party/dart/runtime/BUILD.gn:155`) | **die Dart-VM** | offen – das ist der eigentliche Port |

**Bewertung:** Die Punkte 1 bis 8 waren Konfiguration, zusammen unter einer Stunde. Punkt 9
ist der Aufwand, den `docs/feasibility.md` §4 vorhergesagt hat. Ab hier wird nicht mehr
konfiguriert, sondern portiert.

### Fortsetzung: `gn gen` läuft, Übersetzung beginnt

| # | Fehler | Art | Lösung |
|---|---|---|---|
| 9 | `Unknown target_os: horizon` (Dart) | Konfiguration | `DART_TARGET_OS_HORIZON` in `runtime/BUILD.gn` |
| 10 | `Unsupported ARM OS` (zlib) | Plattformlücke | NEON/CRC32 für horizon aus – die OS-spezifische CPU-Erkennung (`getauxval`) fehlt |
| 11 | `pkg-config` fehlt | Umgebung | ohne root aus dem Container-Image nach `~/bin` |
| — | **`gn gen` fertig: 918 Targets** | | |
| 12 | `-Xclang`, `-fno-cxx-modules` | Toolchain | nur noch unter `is_clang` |
| 13 | `#error Please add support for your platform` | Quellcode | `FML_OS_HORIZON` über `__SWITCH__` |
| 14 | `control reaches end of non-void function` | Toolchain | `-Werror` aus, wie QNX es schon tut |
| 15 | `ULONG_MAX` nicht deklariert | Portabilität | `<climits>` ergänzt |
| 16 | `execinfo.h` fehlt | **Plattformlücke** | offen – glibc-Backtraces gibt es auf Horizon nicht |

**`-Werror`:** QNX nimmt sich davon bereits aus (`compiler/BUILD.gn:749`), und aus genau
demselben Grund – die Warnungsfreiheit der Engine ist gegen Clang geprüft, nicht gegen GCC.
GCC meldet etwa nach einem `switch` über sämtliche Enum-Werte trotzdem
„control reaches end of non-void function" (`fml/cpu_affinity.cc:86`). Das ist eine
Bootstrap-Maßnahme, kein Dauerzustand.

**Beobachtung zur Toolchainwahl:** Die Fehler 12 und 14 wären mit Clang nicht aufgetreten.
Sollte sich diese Klasse häufen, ist der `custom_toolchain`-Weg aus
`docs/gn-target-horizon.md` die Alternative – Clang gegen das devkitA64-Sysroot statt GCC.
Bisher sind es zwei Fälle; das rechtfertigt den Wechsel noch nicht.

### fml übersetzt vollständig

| # | Fehler | Art | Lösung |
|---|---|---|---|
| 16 | `execinfo.h` fehlt | Plattformlücke | Backtrace liefert für Horizon null Frames – leer statt erfunden |
| 17 | `sys/mman.h` in `mapping_posix.cc` | Plattformlücke | eigenes `mapping_horizon.cc`: Datei in Heap-Puffer lesen |
| 18 | `sys/mman.h` in `file_posix.cc` | toter Code | Include entfernt, wird dort nirgends benutzt |
| 19 | `dlfcn.h` | Plattformlücke | eigenes `native_library_horizon.cc`, schlägt sichtbar fehl |
| 20 | `::strdup` nicht deklariert | Toolchain | `-std=gnu++20` statt `-std=c++20`; strenges ANSI blendet newlibs POSIX-Erweiterungen aus |
| 21 | `sys/mman.h` in ICU | Plattformlücke | `U_HAVE_MMAP=0` → ICU nimmt seinen eigenen `MAP_STDIO`-Pfad |
| 22 | `static_cast<pid_t>(pthread_t)` | Plattformunterschied | `pthread_t` ist auf Horizon ein Zeiger; eigener `GetTID`-Zweig in abseil |
| 23 | `link.h` fehlt | Plattformlücke | `__SWITCH__` in die Ausschlussliste von `ABSL_HAVE_ELF_MEM_IMAGE` |

**Ergebnis:** 43 Objektdateien, `ELF64 / AArch64 / REL`, zweiter Lauf meldet
`ninja: no work to do`. Damit übersetzt die Betriebssystemabstraktion der Flutter Engine
für Horizon.

**Muster, das sich abzeichnet:** Fast jede Lücke hat im Upstream bereits einen vorgesehenen
Ausweg, weil andere schlanke Plattformen dasselbe Problem hatten. ICU hat `MAP_STDIO`,
abseil hat eine Ausschlussliste, in der QNX, Haiku und VxWorks schon stehen. Es lohnt sich
also, vor jeder eigenen Lösung zu prüfen, ob der Code den Fall nicht schon kennt.

**Die drei eigenen Implementierungen** liegen unter `patches/flutter-engine/files/` und
werden vom Patch-Skript in den Checkout kopiert. Die wichtigste Abweichung: `FileMapping`
liest die Datei vollständig in den Heap statt sie abzubilden. Das kostet Speicher und
verhindert schreibbare Abbildungen – Letzteres schlägt bewusst mit Protokolleintrag fehl,
statt eine abweichende Zusicherung zu liefern.

### Die Dart-VM übersetzt – und was das nicht heißt

Die vier plattformabhängigen Header waren weniger Arbeit als erwartet: `platform/threads.h`
und `platform/synchronization.h` brauchten nur einen zusätzlichen Zweig, weil libnx über
die Newlib-Syscalls echte pthreads liefert. `platform/utils.h` und `vm/os_thread.h`
bekamen eigene Dateien.

`utils_horizon.h` kommt ohne `<endian.h>` und `<byteswap.h>` aus – newlib hat beides
nicht – und benutzt stattdessen `__builtin_bswap*` mit `__BYTE_ORDER__`. `strerror_r` ist
ebenfalls nicht deklariert; `strerror` ist vertretbar, weil newlib für bekannte Codes
konstante Zeichenketten liefert und der Rückfallpuffer in der pro Thread geführten
Reentrancy-Struktur liegt.

**Ein Konfigurationsfehler, der lange unbemerkt lief:** Der Build lief bis hierher im
Debug/JIT-Modus (`-DFLUTTER_RUNTIME_MODE=1`, `-DFLUTTER_JIT_RUNTIME=1`). Erst
`flutter_runtime_mode="release"` **und** `dart_runtime_mode="release"` schalten auf
Release/AOT. Letzteres ist ein eigenes GN-Argument mit Vorgabe `"develop"`, das die
Flutter-Seite nicht automatisch mitsetzt. Nebenwirkung: Dart schließt damit Perfetto und
den Profiler aus, die beide auf Horizon nicht übersetzen.

**Ergebnis:** `libdart_vm_aotruntime_product` übersetzt vollständig – 675 Ziele, 184
Objektdateien, keine Fehler.

**Und was das nicht heißt.** Dart übersetzt *alle* `os_*.cc`, aber jede verbirgt ihren
Inhalt hinter `#if defined(DART_HOST_OS_...)`. Für Horizon sind `os_linux.o`, `os_win.o`
und Verwandte also leere Objektdateien. Es gibt schlicht kein `os_horizon.cc`.

**Korrektur der ersten Messung.** Zunächst hatte ich nur die undefinierten Symbole
gezählt – das überschätzt deutlich, weil Querverweise zwischen Objektdateien der Normalfall
sind. Nach Abzug der anderswo definierten Symbole (1739 undefiniert, davon 1177 anderswo
definiert) bleibt:

| Bereich | fehlende Funktionen |
|---|---|
| `dart::OS::` | 16 |
| `dart::OSThread::` | 11 |
| `dart::VirtualMemory::` | 7 |
| `dart::CpuInfo` | 5 |
| `dart::NativeSymbolResolver` | 5 |
| `dart::ThreadInterrupter` | **0** |

44 Plattformfunktionen. Gegenüber der ersten Schätzung sind `CpuInfo` und
`NativeSymbolResolver` hinzugekommen, während ein Teil der `OSThread`-Funktionen bereits
in `os_thread.cc` definiert war. Dass `ThreadInterrupter` bei null steht, bestätigt die
frühere Annahme: Der signalbasierte Profiler entfällt im Product-Modus tatsächlich.

Die übrigen 562 offenen Symbole stammen aus anderen Zielen (`dart::Api::`,
`dart::BaseTextBuffer::` und Verwandte) und lösen sich beim Zusammenlinken auf.

### dart:io – Umfang vermessen und Strategie festgelegt

Der Build erreichte `dart/runtime/bin`, die native Seite von `dart:io`: **16
plattformspezifische Dateien, 4671 Zeilen**.

Statt sie zu kopieren wurde zuerst geprüft, welche überhaupt Linux-*spezifische* APIs
benutzen. Ergebnis: `thread_linux.cc`, `utils_linux.cc` und `socket_base_linux.cc`
benutzen **gar keine** – sie sind reiner POSIX-Code unter einem Linux-Wächter.

Deshalb der Ansatz: **Wächter erweitern statt kopieren.** Bei zehn Dateien wurde
`DART_HOST_OS_HORIZON` in die vorhandene Bedingung aufgenommen. Das hält die Abweichung
vom Upstream klein und macht sichtbar, wo Horizon sich wirklich unterscheidet – nämlich
nur dort, wo eine eigene Datei entsteht.

Der Ansatz ist bewusst empirisch: Welche Datei hineingehört, entscheidet der Compiler.
Das ist erheblich schneller, als 4671 Zeilen vorab zu bewerten.

**Ergebnis des ersten Durchlaufs:** Die zehn `.cc`-Dateien übersetzen. Es scheitern nur
die Header, die ihre plattformspezifischen Gegenstücke auswählen – `socket_base.h`
(gelöst, `socket_base_linux.h` umfasst 17 Zeilen reiner Systemincludes) und
`eventhandler.h`.

### Der Eventhandler und sein Weckmechanismus

`eventhandler_linux.cc` (433 Zeilen) setzt **epoll** und **timerfd** voraus, die Horizon
nicht hat. Eine `poll`-basierte Fassung ist der Weg – aber sie hängt an einer Frage, die
vor dem Schreiben zu klären war: Womit weckt ein anderer Thread den wartenden `poll`?
Linux benutzt dafür eine Pipe (`interrupt_fds_[2]`).

Geprüft, was libnx tatsächlich definiert (`nm` über `libnx.a`):

| Funktion | vorhanden |
|---|---|
| `poll` | ja |
| `select` | ja |
| `socketpair` | **ja** |
| `pipe` | **nein** |

**Entscheidung:** `socketpair()` statt Pipe. Ein verbundenes Socket-Paar erfüllt denselben
Zweck – ein Deskriptor, auf den `poll` warten kann und den ein anderer Thread beschreibt –
und ist auf Horizon verfügbar. Der Timer läuft nicht über `timerfd`, sondern über das
Zeitlimit von `poll` selbst, berechnet aus der vorhandenen `TimeoutQueue`.

### Die Dart-Portierung steht

Fünf Dateien, 44 Funktionen, alle Plattformgruppen auf null offene Symbole:

| Datei | Ansatz |
|---|---|
| `os_horizon.cc` | POSIX-Zeitfunktionen (`clock_gettime`, `nanosleep`), stdio. Bewusst **ohne** `<switch.h>`, dessen Makros und Typnamen (`u32`, `Result`) mit Dart-Bezeichnern kollidieren. |
| `os_thread_horizon.cc` | pthreads, eng an der Linux-Fassung |
| `virtual_memory_horizon.cc` | `memalign` aus dem Prozess-Heap, siehe unten |
| `cpuinfo_horizon.cc` | kein `/proc/cpuinfo`; `HasField` meldet `false`, worauf `GetCpuModel()` von selbst auf „Unknown" fällt |
| `native_symbol_horizon.cc` | keine Symbolauflösung zur Laufzeit; alle Anfragen melden „nicht gefunden" |

**Zwei bewusst offen gelassene Lücken**, beide im Code markiert:

`OSThread::GetCurrentStackBounds` liefert `false`. Linux ermittelt die Grenzen über
`pthread_getattr_np`, das newlib nicht hat; libnx kennt sie zwar, aber der Zugriff würde
`<switch.h>` in die Dart-VM ziehen. Die VM wertet `false` als „unbekannt" und fällt bei der
Erkennung von Stapelüberläufen auf konservativere Verfahren zurück. Sollte das zum Problem
werden, ist das die Stelle.

`OS::GetCurrentThreadCPUMicros` liefert `-1`, weil `CLOCK_THREAD_CPUTIME_ID` fehlt. Die VM
wertet das als „nicht verfügbar" aus – ein erfundener Wert wäre schlechter als keiner.

**Ein eigener Fehler, der Zeit kostete:** Meine Idempotenzprüfung im Patch-Skript prüfte
nach dem Fix zuerst den Anker statt das Ergebnis. Beim *Einfügen* bleibt der Anker aber
stehen, also wurden alle fünf Einträge in `vm_sources.gni` doppelt eingetragen und `gn gen`
brach ab. Die Prüfung muss zuerst auf das Ergebnis schauen – aber nur, wenn dieses nicht
leer ist, sonst gilt eine Löschung immer als erledigt. Beide Fälle sind jetzt abgedeckt.

### Entscheidung zum Speichermodell

`virtual_memory_horizon.cc` nimmt den einfachsten Weg, der die Zusicherungen erfüllt:
ausgerichtete Anforderungen über `memalign` aus dem Prozess-Heap. Damit fallen Reserve und
Commit zusammen, `Protect` bleibt wirkungslos und `FreeSubSegment` meldet ehrlich `false`.

Im AOT-Product-Modus ist das vertretbar: Ausführbarer Speicher wird nicht gebraucht – die
Snapshot-Instruktionen liegen in der `.text` der NRO –, und Schreibschutz für Code-Seiten
betrifft nur zur Laufzeit erzeugten Code, den es hier nicht gibt.

**Damit ist die 4-GB-Frage entschieden, und zwar gegen Compressed Pointers.** Deren
Reservierung würde mit dieser Fassung 4 GB tatsächlich belegen statt nur Adressraum zu
reservieren – auf einer Konsole mit 4 GB RAM aussichtslos. Der Build setzt deshalb
`dart_use_compressed_pointers=false`, und die Datei bricht mit `#error` ab, falls diese
Verbindung je unbemerkt zerfällt.

Das ist eine bewusste Bootstrap-Entscheidung, kein Endzustand: libnx hat mit `virtmem` und
`svcMapMemory` die Bausteine für eine echte Trennung von Reservierung und Abbildung. Ohne
Compressed Pointers kostet Dart mehr Speicher pro Objektzeiger – was auf einem Gerät mit
knappem RAM irgendwann zurückkommt.

**Fehler 15 und 16 sind verschiedene Dinge.** `<climits>` ist eine echte
Portabilitätslücke im Upstream-Code, die auf jeder schlanken libc auffiele – newlib zieht
weniger transitiv herein als glibc. `execinfo.h` dagegen ist eine Funktion, die Horizon
schlicht nicht hat.

**Zwei Lehren aus eigenen Fehlern:** Das Einfügen von GN-Zweigen per Index-Schneiden hat
zweimal die schließende Klammer verschluckt, weil der eingefügte Block die ersetzte
Klammer selbst mitbringen muss. Seither nur noch wörtliche Textersetzung mit vollständigem
Kontext. Und: `docker run ... > datei.tar` über PowerShell zerstört Binärdaten – der
Container muss direkt in ein gemountetes Verzeichnis schreiben.

**devkitA64 in WSL ohne root:** `docker run --rm -v "E:\:/out" devkitpro/devkita64 tar -cf
/out/dkp.tar -C /opt devkitpro`, danach in WSL entpacken. 1,2 GB, GCC 15.2.0 läuft nativ.
Damit ist die sudo-Blockade endgültig umgangen.

---

## 2026-08-09 – Meilenstein 1b: Der AOT-Ladeweg trägt

**Die zentrale Hypothese des Projekts ist bis zur Linkphase bestätigt.**

Kette, alle Schritte tatsächlich ausgeführt:

```text
hello.dart
  → gen_kernel (dartaotruntime + vm_platform_product.dill)  → hello.dill, 4,1 MB
  → gen_snapshot --snapshot_kind=app-aot-assembly           → hello_aot.s, 2,47 MB
                                                               121.924 Zeilen
  → aarch64-none-elf-gcc -c (devkitA64 15.2.0)              → hello_aot.o
  → Link mit devkitPro-Regeln                               → aot_poc.nro, 758 KB
```

**Befund 1 – `gen_snapshot` muss nicht selbst gebaut werden.** Die
Android-arm64-Release-Artefakte des gepinnten Flutter-SDK enthalten einen
`gen_snapshot`, der auf x64 läuft und AArch64 erzeugt. Für die *Assembly*-Ausgabe ist
das Zielbetriebssystem unerheblich, nur die Zielarchitektur zählt. Das spart im
PoC einen kompletten Dart-SDK-Build.

**Befund 2 – devkitA64 übersetzt die Ausgabe unverändert.** Die Assembly verwendet
ausschließlich Standard-GNU-Direktiven (`.quad`, `.byte`, `.balign`, `.globl`,
`.type … %object`, `.size`, `.uleb128`, `.cfi_*`). Keine Anpassung, kein Patch, keine
Warnung.

**Befund 3 – die Instruktionen landen im ausführbaren Segment.** `nm` auf der fertigen
ELF:

```text
00000000000003c0 T _kDartVmSnapshotInstructions
0000000000016e00 T _kDartIsolateSnapshotInstructions
0000000000078500 R _kDartVmSnapshotData
000000000007cd00 R _kDartIsolateSnapshotData
```

`T` bedeutet `.text`. Damit liegt der AOT-Code in dem Bereich, den der Homebrew-Loader
ohnehin ausführbar mappt – **kein `dlopen`, kein `mmap`, kein `mprotect`, kein `jit.h`**.
Genau das war in `docs/feasibility.md` §1 als Hypothese formuliert.

**Befund 4 – Namensfalle für den anderen Weg.** `gen_snapshot` erzeugt die Symbole mit
führendem Unterstrich (`_kDartVmSnapshotData`), die Engine sucht per dlsym dagegen ohne
(`dart_snapshot.cc:18-21`). Für unseren Weg irrelevant, weil wir Zeiger übergeben statt
Namen – für den ELF-/`dlopen`-Weg wäre es eine stille Fehlerquelle gewesen.

**Befund 5 – auf Hardware bestätigt.** Die `.nro` auf der Konsole gestartet, Adressen und
erste Bytes zur Laufzeit ausgelesen:

```text
kDartVmSnapshotData              0x57f0ef740  f5 f5 dc dc cc 41 00 00
kDartVmSnapshotInstructions      0x57f06ec40  20 6a 01 00 00 00 00 00
kDartIsolateSnapshotData         0x57f0f3f40  f5 f5 dc dc ff 6b 02 00
kDartIsolateSnapshotInstructions 0x57f085680  a0 f9 03 00 00 00 00 00
Adresse von main()               0x57f06e180
```

Zwei Dinge daran zählen:

`f5 f5 dc dc` ist little-endian `0xdcdcf5f5` – die Snapshot-Magic der Dart-VM
(`runtime/vm/snapshot.h:37`). Genau diesen Wert liest `SnapshotHeaderReader::IsValid()`
als Erstes. Die Daten haben Übersetzung, Linken, NRO-Verpackung und Laden also unbeschädigt
überstanden.

`kDartVmSnapshotInstructions` liegt 0xAC0 Bytes hinter `main()`. Dart-AOT-Code und unser
C++-Code liegen damit im selben Modul und im selben ausführbar gemappten Bereich. Der
Homebrew-Loader hat den Snapshot ausführbar gemappt, ohne dass wir etwas dafür tun mussten.

**Was das ausdrücklich nicht zeigt:** dass der Snapshot *läuft*. Dafür fehlt die Dart-VM
für Horizon. Geprüft ist der Ladeweg, nicht die Ausführung. Der nächste Beweis dafür
kommt erst mit Meilenstein 2.

---

## 2026-08-09 – Logausgabe: Richtung umdrehen statt gegen NAT kämpfen

**Problem:** `nxlink -s` leitet stdout der Konsole zum Entwicklungsrechner um, indem die
Switch sich zum Absender *zurück*verbindet. Läuft nxlink in einem Container, ist der
Absender hinter NAT und nicht erreichbar – die Ausgabe kommt nie an. Mit
`--network host` funktioniert zwar der Upload, aber nicht der Rückkanal.

**Lösung:** Eine TCP-Senke im Logging, bei der die **Switch** die Verbindung aufbaut
(`LogConfig::remote_host`/`remote_port`). Ausgehende Verbindungen gehen durch jedes NAT.
Empfänger ist `scripts/log-listener.ps1`.

Zwei Eigenschaften, die sich beim Debuggen auszahlen: Der Verbindungsaufbau hat zwei
Sekunden Zeitlimit, damit ein nicht laufender Empfänger nicht den Anwendungsstart
blockiert. Und bricht die Verbindung im Betrieb weg, legt sich nur diese Senke still,
während Datei und Konsole weiterlaufen.

Die Senke sitzt im Embedder-Logging, nicht im Beispiel – der spätere Flutter-Host bekommt
sie damit geschenkt, inklusive der Engine-Ausgabe über `FlutterLogMessageCallback`.

---

## 2026-08-09 – Falle: frischer `PadState` meldet gehaltene Tasten als Neudruck

**Befund:** Ein neu per `padInitializeDefault` angelegter `PadState` startet mit leerem
Vorzustand (`buttons_old == 0`). Ist eine Taste beim ersten `padUpdate` noch physisch
gedrückt, meldet `padGetButtonsDown` sie als *neuen* Druck.

**Symptom:** Der Diagnoseschirm von `hello_libnx` verschwand sofort wieder – er wird nach
einem Plus-Druck geöffnet, und genau dieses noch gehaltene Plus beendete ihn im selben
Moment. Auf dem Fernseher: kurz schwarz, dann hbmenu.

**Konsequenz:** Nach dem Anlegen eines `PadState` einmal `padUpdate` zum Einlesen des
Startzustands aufrufen und, wenn ein Tastendruck den Kontextwechsel ausgelöst hat, erst
die Freigabe abwarten. Für den späteren Flutter-Eingabepfad ist dieselbe Falle relevant:
Pointer- und Key-Events dürfen nicht aus einem uninitialisierten Vorzustand abgeleitet
werden, sonst erzeugt jeder Fokuswechsel Phantom-Events.

**Noch offen:** Logausgabe wurde noch nicht gesehen, sauberes Beenden über Plus noch nicht
bestätigt, und der Framebuffer-Stride ist noch nicht gegen `width * 4` geprüft. Genau
dieser Wert entscheidet später, ob Flutters Zeilenabstand direkt durchgereicht werden kann.

---

## 2026-08-09 – devkitPro baut standardmäßig ohne RTTI und ohne Exceptions

**Befund:** Das offizielle Anwendungs-Template setzt
`CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions`
(`third_party/reference/switch-application.Makefile:57`). Ebenso bestätigt es, dass
`-D__SWITCH__` aus dem Makefile kommt (`:55`) und nicht vom Compiler – siehe
`docs/gn-target-horizon.md`.

**Konsequenz:** Die Coding-Regel „keine Exceptions voraussetzen, bevor geprüft ist, ob wir
sie im Ziel-Build wollen" ist damit beantwortet: Der Homebrew-Standard ist ohne. Unser
Embedder hält sich daran. Für den Engine-Build heißt das, dass Engine, Dart-VM und Skia
mit denselben Einstellungen gebaut werden müssen – gemischte Übersetzungseinheiten
(einige mit, andere ohne Exceptions) sind eine der unangenehmeren Fehlerquellen beim
Linken. Zu prüfen, sobald der Engine-Build steht.

---

## 2026-08-09 – Dart braucht im AOT-Modus keinen ausführbaren Speicher (mit einer Ausnahme)

Geprüft gegen `dart-lang/sdk` @ `02abc578` (Shallow-Checkout unter `~/dart-sdk-ref` in WSL).

**Befund 1 – Snapshot-Instruktionen:** `PageSpace::SetupImagePage`
(`runtime/vm/heap/pages.cc:1527-1560`) legt für die AOT-Instruktionen eine Page über
`VirtualMemory::ForImagePage(pointer, size)` an. Solche Regionen haben
`vm_owns_region() == false` – die VM ändert weder Schutz noch gibt sie sie frei
(`runtime/vm/virtual_memory.h:143-148`). Aufgerufen wird das aus
`app_snapshot.cc:9987-10096` mit `is_executable=true`, aber ohne jede Allokation.

**Konsequenz:** In die `.text` der NRO gelinkte Instruktionen sind genau das, was die VM
hier erwartet. Kein `mmap`, kein `mprotect`, kein `PROT_EXEC` zur Laufzeit.
Einzige Bedingung: `ShouldDualMapExecutablePages()` muss `false` sein – das ist es, weil
`DART_ENABLE_RX_WORKAROUNDS` nur für iOS+JIT definiert wird (`virtual_memory.h:19-40`).

**Befund 2 – Heap:** Ausführbare Heap-Pages (`PageSpace::AllocatePage(is_exec=true)`)
entstehen nur beim Kompilieren zur Laufzeit, also nicht im Precompiled Runtime.

**Befund 3 – FFI-Callbacks (die Ausnahme):** `FfiCallbackMetadata` allokiert
Trampolin-Seiten und flippt sie außerhalb von macOS per `Protect()` von RW nach RX
(`runtime/vm/ffi_callback_metadata.cc:152-165`). Das passiert aber **lazy**:
`FfiCallbackMetadata::Init()` (`dart.cc:393`) legt nur das Singleton an
(`ffi_callback_metadata.cc:103-107`); die Seite entsteht erst in
`EnsureFreeListNotEmptyLocked()`, wenn tatsächlich ein `NativeCallable` erzeugt wird.

**Konsequenz:** Eine Flutter-App ohne FFI-Callbacks braucht nie ausführbaren Speicher.
Apps mit `NativeCallable` brauchen ihn – und Horizons `jit.h` bietet ein RW/RX-*Doppel*mapping,
während Dart hier das Umschalten *einer* Adresse erwartet. Das ist eine dokumentierte
Einschränkung für später, kein MVP-Blocker.

---

## 2026-08-09 – Compressed Pointers verlangen 4 GB Adressraum

**Befund:** Bei `DART_COMPRESSED_POINTERS` reserviert `VirtualMemory::Init()`
`kCompressedHeapSize` mit `kCompressedHeapAlignment` und bricht sonst mit `FATAL` ab
(`runtime/vm/virtual_memory_posix.cc:565-577`). Beide Konstanten sind **4 GB**
(`runtime/vm/virtual_memory_compressed.h`). Auf arm64 sind Compressed Pointers der
Standard.

**Konsequenz:** Entweder eine 4-GB-ausgerichtete 4-GB-Reservierung im Horizon-Adressraum
(reine Buchhaltung wäre über libnx `virtmem.h` denkbar, da dort Reservierung und
tatsächliches Mapping ohnehin getrennt sind), oder Dart mit
`dart_use_compressed_pointers=false` bauen – kostet Speicher, entschärft aber ein Risiko.
Entscheidung fällt, sobald der Engine-Build steht; vorher nicht spekulieren.

**Nebenbefund:** Dart trennt `Reserve()` / `Commit()` / `Decommit()`
(`virtual_memory.h:157-159`). Diese Aufteilung passt gut zu libnx, wo
Adressraum-Buchhaltung (`virtmem`) und echtes Mapping (`svcMapMemory`) ebenfalls getrennt sind.

---

## 2026-08-09 – Engine-Quellen liegen lokal vor

**Befund:** Das installierte Flutter-SDK (`C:\Users\mirkorichter\flutter`, Tag `3.41.6`)
ist das Monorepo und enthält `engine/src/flutter` vollständig, inklusive `build/`-Konfiguration.
Es fehlen nur die `gclient`-Abhängigkeiten (Dart SDK, Skia, buildtools).

**Konsequenz:** Engine-Analyse ist offline und exakt gegen die gepinnte Version möglich.
Für den eigentlichen Build braucht es trotzdem einen separaten `gclient`-Checkout auf dem
Linux-Host, weil der SDK-Checkout kein `.gclient` besitzt.

---

## 2026-08-09 – AOT-Snapshot per Symbolreferenz wird ausgewertet

**Befund:** `FlutterProjectArgs.vm_snapshot_instructions` &co. werden in
`shell/platform/embedder/embedder.cc:1817-1837` in `Settings` übertragen und in
`runtime/dart_snapshot.cc:102-177` über den Embedder-Callback aufgelöst, *vor* Dateipfaden
und vor `fml::NativeLibrary` (`dlopen`).

**Konsequenz:** Der ELF-/`dlopen`-Weg (`kFlutterEngineAOTDataSourceTypeElfPath`, der
einzige Typ in `FlutterEngineAOTDataSource`) ist vermeidbar. Siehe `docs/feasibility.md` §1.

---

## 2026-08-09 – QNX als Vorlage für ein neues Target-OS

**Befund:** `engine/src/build/config/BUILDCONFIG.gn:313-325` definiert `current_os == "qnx"`
mit eigenem `is_*`-Block; `fml/BUILD.gn:199-201` fügt für QNX genau eine Quelldatei hinzu.
Betroffen sind insgesamt ~11 Dateien (4 in `build/`, 7 `.gn` in `flutter/`).

**Konsequenz:** `current_os = "horizon"` ist ein kleiner, sauber isolierbarer Patch.
Patches gehören nach `patches/flutter-engine/`.

---

## 2026-08-09 – libnx liefert echte pthreads

**Befund:** `nx/source/runtime/newlib.c` implementiert `__syscall_thread_create`,
`__syscall_thread_join`, `__syscall_thread_exit`, `__syscall_thread_self` auf libnx-`Thread`.
Thread-Stacks müssen 4K-aligned sein (ebd. Zeile 166).

**Konsequenz:** `fml` kann die POSIX-Threadschicht nutzen. Die Dart-VM braucht dennoch
eigene `os_thread_horizon.{cc,h}`, weil sie keine generische POSIX-Variante besitzt
(nur `os_thread_absl` als Alternative, Abseil ist auf libnx nicht verfügbar).
