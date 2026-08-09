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
