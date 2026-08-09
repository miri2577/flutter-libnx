# Machbarkeitsanalyse (Meilenstein 0)

Stand: 2026-08-09. Alle Aussagen unten sind gegen tatsächlichen Quellcode der gepinnten
Version geprüft. Wo etwas nicht geprüft wurde, steht das ausdrücklich dabei.

## 0. Gepinnte Versionen

| Komponente | Version / Commit | Quelle |
|---|---|---|
| Flutter (Monorepo, Framework + Engine) | `db50e20168db8fee486b9abf32fc912de3bc5b6a`, Tag `3.41.6`, Channel stable | lokales SDK `C:\Users\mirkorichter\flutter`, `git describe` |
| Dart SDK | `02abc57898bebc334a997e609ce5827c8ef207d7` (Dart 3.11.4) | `DEPS`, `dart_revision` |
| Skia | `a183ded9ad67d998a5b0fe4cd86d3ef5402ffb45` | `DEPS`, `skia_revision` |
| Engine-Artefakt-Hash (Prebuilts) | `5cdd32777948fa7a648fac915f8da7120ac7e97a`, Revision `425cfb54d0` | `flutter --version` |
| libnx (Referenz) | `dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb` (master) | `third_party/libnx` |
| devkitA64 / devkitPro | **noch nicht installiert** | – |
| Build-Host | WSL2 Ubuntu 24.04.4, x86_64, 4 Cores, 7 GB RAM, 943 GB frei | `uname`, `df`, `free` |

Der entscheidende Glücksfall: das installierte Flutter-SDK ist das Monorepo und enthält
`engine/src/flutter` vollständig. Die Engine-C++-Quellen der gepinnten Version liegen also
bereits lokal vor. Es fehlen nur die per `gclient` gezogenen Abhängigkeiten
(Dart SDK, Skia, buildtools).

## 1. Der erwartete Hauptblocker ist entschärft: AOT ohne `dlopen`

**Erwartung vorab:** Dart AOT braucht eine ELF-Shared-Library, die zur Laufzeit per
`dlopen` geladen und deren Instruktions-Section als `PROT_EXEC` gemappt wird. Horizon
Homebrew hat weder `dlopen` noch `mmap` mit `PROT_EXEC` im Normalfall.

**Befund:** Die Embedder-API bietet zwei getrennte Wege, und der zweite vermeidet das
Problem vollständig.

- Weg A (der übliche): `FlutterEngineCreateAOTData` – kennt in dieser Version genau
  *einen* Quelltyp: `kFlutterEngineAOTDataSourceTypeElfPath`
  (`third_party/reference/embedder.h:2426-2439`). Braucht ELF-Loading. Für uns schlecht.
- Weg B: `FlutterProjectArgs.vm_snapshot_data` / `vm_snapshot_instructions` /
  `isolate_snapshot_data` / `isolate_snapshot_instructions`
  (`embedder.h:2511-2542`). Die Doku dort sagt explizit: *"If … is a symbol reference,
  0 may be passed here."* Der Instruktions-Puffer muss lediglich
  *read-execute gemappt* sein – nicht zur Laufzeit gemappt *werden*.

Weg B wird in dieser Version tatsächlich ausgewertet, nicht nur deklariert:
`engine/src/flutter/shell/platform/embedder/embedder.cc:1817-1837` überträgt die Felder in
`settings.vm_snapshot_data` / `settings.vm_snapshot_instr` usw.; `runtime/dart_snapshot.cc:102-177`
löst sie über den `embedder_mapping_callback` auf, *bevor* Dateipfade oder
`NativeLibrary`/`dlopen` überhaupt versucht werden (`SearchMapping`, ebd. Zeile 53-98).

**Konsequenz für die Switch:** Der AOT-Snapshot kann mit
`gen_snapshot --snapshot_kind=app-aot-assembly` als AArch64-Assembly erzeugt, mit dem
devkitA64-Assembler übersetzt und **direkt in die `.nro` gelinkt** werden. Die
Instruktionen liegen dann in der `.text` der NRO, die der Homebrew-Loader ohnehin
read-execute mappt. Kein `dlopen`, kein Laufzeit-`mprotect`, kein `jit.h` nötig.

Zusätzlich existiert in der Engine ein Build-Modus `DART_SNAPSHOT_STATIC_LINK`
(`runtime/dart_snapshot.cc:27,104-177`), der genau dieses Muster mit festen Symbolen
(`kDartVmSnapshotInstructions` usw.) bereits vorsieht. Wir brauchen ihn vermutlich nicht,
weil der Embedder-API-Weg dasselbe erreicht, aber er ist ein Rückfallpfad.

**Noch nicht verifiziert (nächster Prüfschritt):**
- ob `gen_snapshot` der gepinnten Dart-Version `app-aot-assembly` für ein Target
  erzeugen kann, dessen OS es nicht kennt (Assembly-Ausgabe ist ELF/GNU-`as`-Syntax,
  sollte OS-neutral genug sein – ungeprüft),
- ob der devkitA64-Linker die erzeugten Sections/Alignments akzeptiert,
- ob die Dart-VM im AOT-Product-Mode wirklich *keine* ausführbaren Allokationen mehr
  vornimmt.

## 2. Software Renderer ist in der gepinnten Version vorhanden

`FlutterSoftwareRendererConfig` mit

```c
typedef bool (*SoftwareSurfacePresentCallback)(void*  /* user data */,
                                               const void* /* allocation */,
                                               size_t /* row bytes */,
                                               size_t /* height */);
```

(`embedder.h:613-616`, `1020-1038`) existiert unverändert; `kSoftware` ist ein gültiger
`FlutterRendererType` (`embedder.h:79-81`). Es gibt zusätzlich
`kFlutterBackingStoreTypeSoftware` und `…Software2` für den Compositor-Weg
(`embedder.h:2051-2061`). Der Engine-seitige Unterbau ist vorhanden
(`shell/platform/embedder/embedder_surface_software.cc`).

Skia ist in dieser Version weiterhin regulärer Bestandteil (`DEPS`, `skia_revision`).

**Upgrade-Risiko (wichtig):** Flutter 3.44 hat den Skia-Backend für Android 10+ entfernt;
Impeller ist dort verpflichtend. Ob und wie lange der *Software*-Renderer für
Embedder-API-Nutzer erhalten bleibt, ist damit offen. Das ist ein Argument dafür, 3.41.6
als Bootstrap-Version zu pinnen und einen späteren Upgrade-Pfad separat zu bewerten –
nicht dafür, sofort auf Impeller zu gehen (Impeller braucht Vulkan/GLES, was den
schwierigsten Teil ans Projektanfang ziehen würde).

## 3. Es gibt einen Präzedenzfall für ein neues Target-OS: QNX

Die Engine hat mit QNX ein relativ frisches, nicht-Linux-Target. Der Umfang ist klein:

- `engine/src/build/config/BUILDCONFIG.gn:313-325` – ein Block pro OS, der die
  `is_*`-Flags setzt. QNX setzt dort sogar `is_posix = false` und `is_qnx = true`.
- `engine/src/build/toolchain/qnx/BUILD.gn` – eigene Toolchain-Definition.
- `engine/src/build/build_config.h`, `engine/src/build/config/compiler/BUILD.gn`
- Engine-seitig nur 7 `.gn`-Dateien betroffen (`flutter/BUILD.gn`, `fml/BUILD.gn`,
  `skia/BUILD.gn`, `shell/platform/BUILD.gn`, `impeller/...`).
- In `fml` kostet QNX ganze **eine** Datei: `platform/qnx/paths_qnx.cc`
  (`fml/BUILD.gn:199-201`). Der Rest läuft über die POSIX-Schicht.

Das ist die Blaupause für `current_os = "horizon"`. Es ersetzt die verbotene Abkürzung
`#define __linux__` durch einen sauberen, kleinen, dokumentierbaren Eingriff.

## 4. Der eigentliche Aufwand liegt in der Dart-VM

Die Dart-VM hat keine generische POSIX-Schicht, sondern Dateien pro OS
(geprüft via GitHub-API gegen `dart_revision` `02abc578`):

| Datei-Familie | vorhanden für | für Horizon nötig |
|---|---|---|
| `os_<os>.cc` | android, fuchsia, linux, macos, win | **ja** – neu |
| `os_thread_<os>.{cc,h}` | android, fuchsia, linux, macos, win, `absl` | **ja** – neu (pthread-basiert) |
| `cpuinfo_<os>.cc` | android, fuchsia, linux, macos, win | **ja** – neu (trivial) |
| `thread_interrupter_<os>.cc` | android, fuchsia, linux, macos, win | nur für Profiler – im Product-Build vermutlich abschaltbar |
| `virtual_memory_posix.cc` | generisch POSIX (`mmap`/`mprotect`) | **kritisch** – siehe unten |

`virtual_memory_posix.cc` ist der einzige generische POSIX-Teil und setzt `mmap`,
`mprotect`, `munmap` voraus. libnx bietet kein `mmap`. Es bietet aber die Bausteine, um
eine ausreichend große Teilmenge nachzubauen: `virtmem.h` (Adressraum-Reservierung),
`svcMapMemory`/`svcSetMemoryPermission` und – falls doch ausführbarer Speicher gebraucht
wird – `jit.h` mit RW/RX-Doppelmapping (`jitCreate`, `jitTransitionToWritable`,
`jitTransitionToExecutable`, geprüft in `third_party/libnx/nx/include/switch/kernel/jit.h`).

**Nachtrag 2026-08-09 – diese Frage ist beantwortet.** Gemessen am Dart-Checkout
`02abc578` (Details in `docs/porting-notes.md`):

- Die zu implementierende Schnittstelle ist klein: `Reserve`, `Commit`, `Decommit`,
  `FreeSubSegment`, `AllocateAligned`, `Protect`, `DontNeed`, `Truncate`, `Init`,
  `CalculatePageSize` (`virtual_memory.h:95-159`). Die Trennung Reserve/Commit passt
  strukturell zu libnx (`virtmem` für Adressraum, `svcMapMemory` für echtes Mapping).
- **Ausführbarer Speicher wird im AOT-Product-Mode nicht gebraucht.** Die
  Snapshot-Instruktionen laufen über `VirtualMemory::ForImagePage`, deren Region der VM
  nicht gehört und die sie nie umschützt (`pages.cc:1527`, `virtual_memory.h:143-148`).
  Ausführbare Heap-Pages entstehen nur beim Kompilieren zur Laufzeit.
- Einzige Ausnahme: FFI-Callbacks (`NativeCallable`) allokieren Trampolinseiten und
  flippen sie RW→RX – aber **lazy**, erst bei tatsächlicher Nutzung
  (`ffi_callback_metadata.cc:103-107`, `dart.cc:393`). Kein MVP-Blocker.
- Neu aufgetaucht: Compressed Pointers (arm64-Standard) verlangen eine **4 GB große,
  4 GB ausgerichtete Reservierung**, sonst `FATAL` (`virtual_memory_posix.cc:565-577`).
  Das ist jetzt das größte Einzelrisiko im Speichermodell. Ausweg vorhanden:
  `dart_use_compressed_pointers=false`.

## 5. Threading: unerwartet gut

libnx implementiert die Newlib-Thread-Syscalls und damit echte pthreads:
`third_party/libnx/nx/source/runtime/newlib.c` definiert u.a.
`__syscall_thread_create`, `__syscall_thread_join`, `__syscall_thread_exit`,
`__syscall_thread_self` auf Basis von libnx-`Thread`. Dazu kommen native Primitive:
`mutex.h`, `condvar.h`, `rwlock.h`, `semaphore.h`, `barrier.h`, `event.h`, `wait.h`.

Damit sind die Anforderungen von `fml` (Threads, Mutex, Condvar, TLS) und der Dart-VM
grundsätzlich abbildbar. Einschränkung, die früh gemessen werden muss: Thread-Stacks
müssen 4K-aligned sein (`newlib.c:166`), und Horizon-Threads sind an CPU-Cores gebunden –
Homebrew hat regulär nur 3 der 4 Cores zur Verfügung.

## 6. Rendering-Ausgabe auf der Switch

Für den Software-Renderer reicht `display/framebuffer.h` aus libnx
(`framebufferCreate`/`framebufferMakeLinear`/`framebufferBegin`/`framebufferEnd`).
Das ist der einfachste Weg, einen RGBA-Puffer bei 1280×720 anzuzeigen. Ein
Format-/Swizzle-Abgleich zwischen Flutters „native 32-bit RGBA" und dem
Switch-Framebuffer-Format ist zu erwarten und wird in Meilenstein 1 praktisch geklärt.

## 7. Bewertung

| Frage | Antwort |
|---|---|
| Ist das Ziel prinzipiell erreichbar? | Ja, aber mit erheblichem Aufwand. Kein bekannter fundamentaler Show-Stopper. |
| Größter Aufwand | Dart-VM-OS-Port (`os_*`, `os_thread_*`, `VirtualMemory`) |
| Größtes verbleibendes Risiko | 4-GB-Reservierung für Compressed Pointers im Horizon-Adressraum (Ausweg: ohne Compressed Pointers bauen) |
| Zweitgrößtes Risiko | Build-Host-Ressourcen: 4 Cores, WSL2 mit 7 von 16 GB Host-RAM; Link-Phasen können OOM laufen |
| Entschärft (Nachtrag) | `VirtualMemory`-Schnittstelle ist klein; ausführbarer Speicher im AOT-Product-Mode nicht nötig |
| Entschärft | AOT-Laden ohne `dlopen`/`PROT_EXEC`; Software-Renderer vorhanden; Threading vorhanden; Blaupause für neues Target-OS vorhanden |
| Kein Präzedenzfall gefunden | Es existiert kein bekanntes öffentliches Flutter-auf-Switch-Homebrew-Projekt. Wir haben keine fremden Patches, an denen wir uns orientieren können. |

## 8. Nächste Prüfschritte (in dieser Reihenfolge)

1. devkitPro installieren, `hello_libnx.nro` bauen – klärt Toolchain und Framebuffer
   praktisch (Meilenstein 1).
2. ~~Dart-SDK-Quellen auschecken, `virtual_memory_posix.cc` und ihre Aufrufer im
   AOT-Product-Pfad lesen~~ – **erledigt 2026-08-09**, siehe Nachtrag in §4.
3. `gen_snapshot` der gepinnten Version besorgen und `app-aot-assembly` für arm64 erzeugen,
   dann testweise mit devkitA64 assemblieren und in eine `.nro` linken – klärt den
   AOT-Weg praktisch, *bevor* die Engine überhaupt gebaut werden muss.

Schritt 3 ist bewusst früh: Er testet die zentrale Hypothese aus Abschnitt 1 mit sehr
geringem Aufwand und ohne den kompletten Engine-Build.
