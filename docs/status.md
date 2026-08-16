# Status

Stand: 2026-08-16

Nur abgehakt, was tatsächlich ausgeführt und überprüft wurde.

## Meilenstein 0 – Repository und Machbarkeitsanalyse

- [x] Repository-Struktur angelegt
- [x] Flutter-Engine-Version gepinnt und dokumentiert (3.41.6 / `db50e20168`)
- [x] Engine-Quellen lokal verfügbar (Monorepo im installierten Flutter-SDK)
- [x] `embedder.h` der gepinnten Version analysiert (Software Renderer, AOT, Task Runner)
- [x] AOT-Ladeweg ohne `dlopen` in der Engine-Quelle verifiziert
- [x] Präzedenzfall für neues Target-OS in der Engine gefunden (QNX)
- [x] libnx-Referenz-Checkout, Threading-/JIT-/Virtmem-Fähigkeiten geprüft
- [x] Portability Matrix (erste Fassung)
- [x] Dart-SDK-Quellen ausgecheckt (`~/dart-sdk-ref` in WSL, @ `02abc578`)
- [x] `VirtualMemory`-Bedarf der Dart-VM im AOT-Modus vermessen
- [x] geklärt: ausführbarer Speicher im AOT-Product-Mode nicht nötig (außer FFI-Callbacks)
- [x] Toolchain nutzbar – über das Container-Image `devkitpro/devkita64` statt lokaler
      Installation, damit ohne root (`scripts/dkp.ps1`)

## Meilenstein 1 – Minimaler libnx Host

- [x] Quellcode geschrieben (`embedder/src/log.cpp`, `embedder/src/platform/switch_platform.cpp`,
      `examples/hello_libnx/`) – **ungebaut**, Toolchain fehlt noch
- [x] alle benutzten libnx-Symbole gegen die echten Header verifiziert (24 Stück)
- [x] **`hello_libnx.nro` kompiliert** – 214 KB, gültiges `NRO0`-Magic, null Warnungen
      bei `-Wall`, sauberer Rebuild reproduziert
- [x] **startet auf echter Hardware** (Switch, Atmosphère, Applet-Modus über Album),
      übertragen per `nxlink` – Framebuffer zeigt das erwartete Bild
- [x] **Controller-Eingabe reagiert** (A → gelbe Fläche)
- [x] Bild wird tatsächlich fortlaufend neu gezeichnet (wandernder Balken)
- [x] **sauberes Beenden über Plus**
- [x] **Framebuffer-Stride gemessen: 5120 Bytes = `1280 * 4`** – Flutters Puffer kann
      direkt durchgereicht werden, kein zeilenweises Umkopieren nötig
- [x] **Prozessspeicher gemessen** – siehe `docs/hardware-target.md`.
      **Korrigiert am 2026-08-11:** Die hier ursprünglich vermerkten „3007 MB
      im Applet-Modus" stimmen nicht. Gemessen vor `FlutterEngineInitialize`:
      Applet-Modus **380 MB**, Anwendungsmodus (hbmenu über ein Spiel mit
      gehaltener R-Taste) **3189 MB**. Der Unterschied ist der Grund, warum das
      Framework im Applet-Modus an `pthread_create` scheitert – jeder
      Engine-Thread will 2 MB Stack.
- [x] **Logging liefert Ausgabe am Entwicklungsrechner** – über eine TCP-Senke, bei der
      die Switch die Verbindung aufbaut (`scripts/log-listener.ps1`). `nxlink -s`
      scheitert an Docker-NAT, siehe `docs/porting-notes.md`.

## Meilenstein 1b – AOT-Assembly-PoC

- [x] `gen_snapshot` für AArch64 gefunden (Android-arm64-Artefakte des gepinnten SDK,
      kein eigener Dart-Build nötig)
- [x] Dart-Kernel erzeugt (`gen_kernel_aot` + `vm_platform_product.dill`)
- [x] AOT-Snapshot als AArch64-Assembly erzeugt (2,47 MB, 121.924 Zeilen)
- [x] mit devkitA64 assembliert – ohne Anpassung, ohne Warnung
- [x] in eine `.nro` gelinkt (758 KB), alle vier Symbole in der ELF vorhanden
- [x] **Instruktionen liegen als `T` in `.text`** – kein `dlopen`/`mmap`/`mprotect` nötig
- [x] **auf Hardware gestartet, Symboladressen zur Laufzeit geprüft** – Instruktionen
      liegen 0xAC0 Bytes hinter `main()`, also im selben ausführbaren Mapping
- [x] **Snapshot-Magic `0xdcdcf5f5` zur Laufzeit korrekt** – die Daten überstehen
      Linken, NRO-Verpackung und Laden unbeschädigt
- [ ] Snapshot *ausgeführt* – braucht die Dart-VM, also Meilenstein 2

## Meilenstein 2 – Engine Cross-Compile PoC

- [x] Engine-Checkout (`gclient sync`) auf dem Build-Host – 26 GB, `~/engine/flutter`
- [x] devkitA64 ohne root in WSL verfügbar (aus dem Container-Image entpackt)
- [x] `current_os = "horizon"` in `BUILDCONFIG.gn` definiert
- [x] `build/toolchain/horizon` nach QNX-Vorbild (gcc_toolchain, `is_clang = false`)
- [x] `flutter/shell/platform/BUILD.gn` kennt horizon
- [x] **`gn gen` läuft durch** – 918 Targets aus 304 Dateien
- [x] `pkg-config` ohne root beschafft (aus dem Container-Image nach `~/bin`)
- [x] devkitA64 übersetzt Engine-Quellen (erste `.o`-Dateien liegen vor)
- [x] `FML_OS_HORIZON` in `fml/build_config.h`
- [x] **fml kompiliert vollständig** – 43 Objektdateien, AArch64 ELF64,
      `ninja: no work to do` beim zweiten Lauf
- [x] eigene fml-Plattformquellen für Horizon (`mapping_horizon.cc`,
      `native_library_horizon.cc`)
- [x] libnx-Include-Pfad und Switch-Architekturflags in der Compilerkonfiguration
      (`-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec`)
- [x] Horizon in allen vier OS-Erkennungen von `dart/runtime/platform/globals.h`
- [x] **die vier plattformabhängigen Dart-Header** – `platform/utils.h`,
      `platform/threads.h`, `platform/synchronization.h`, `vm/os_thread.h`;
      zwei brauchten nur einen pthread-Zweig, zwei eine eigene Datei
      (`utils_horizon.h`, `os_thread_horizon.h`)
- [x] Build läuft im **Release/AOT-Modus** (`flutter_runtime_mode=release`,
      `dart_runtime_mode=release`) – vorher lief versehentlich Debug/JIT
- [x] **`libdart_vm_aotruntime_product` übersetzt vollständig** – 675 Ziele, 184
      Objektdateien, keine Fehler
- [x] **Dart-VM-Portierung vollständig** – fünf neue Dateien (`os_horizon.cc`,
      `os_thread_horizon.cc`, `virtual_memory_horizon.cc`, `cpuinfo_horizon.cc`,
      `native_symbol_horizon.cc`). Alle Plattformgruppen stehen auf **0** offenen
      Symbolen: `OS::`, `OSThread::`, `VirtualMemory::`, `CpuInfo`,
      `NativeSymbolResolver`.
- [x] Compressed Pointers abgeschaltet (`dart_use_compressed_pointers=false`) –
      Begründung in `docs/porting-notes.md`
- [x] **Dart VM übersetzt vollständig** (AOT-Product-Modus)
- [x] **`dart:io` portiert** – `poll`-basierter Eventhandler statt epoll,
      `socketpair` statt Pipe, eigene `stdio`- und `socket_base`-Fassungen
- [x] **Skia übersetzt** (Software-Konfiguration, kein GL/Vulkan)
- [x] **Die komplette Engine übersetzt** – 3184 Objektdateien, AArch64 ELF64.
      Alle Kernsymbole der Embedder-API vorhanden (`FlutterEngineRun`,
      `FlutterEngineInitialize`, `SendWindowMetricsEvent`, `SendPointerEvent`,
      `SendPlatformMessage`).
- [x] **statisch linkbare Bibliothek** – eigenes Ziel `flutter_engine_static`
      (`complete_static_lib`), 1,6 GB, 2909 Objektdateien
- [x] **Engine linkt in eine `.nro`** – `examples/engine_link_test`, 3,0 MB,
      gültiges `NRO0`-Magic, AArch64. Aufrufe in die Engine
      (`FlutterEngineGetCurrentTime`, `FlutterEngineRunsAOTCompiledDartCode`)
      sind im Programm enthalten.
- [x] **die vollständige Startsequenz linkt** – `FlutterEngineInitialize` wird
      aufgerufen und ist in der ELF enthalten; die NRO wächst dadurch von 3,0 auf
      7,4 MB. Kein einziges offenes Symbol mehr.
- [x] **Verzeichnis-Deskriptoren nachgebaut** – Horizon kennt keine, weil devoptab
      Dateien und Verzeichnisse strikt trennt. Eigener Handle-Bereich plus
      `--wrap` für `dup`/`close`/`fstat` in
      `embedder/src/platform/posix_compat_horizon.cpp`
- [x] **Nachrichtenschleife für Horizon** – `MessageLoopImpl::Create()` fiel für
      Horizon in den `#else`-Zweig und lieferte `nullptr`; das linkt sauber und
      stürzt erst zur Laufzeit ab. Ersatz auf Basis von `std::condition_variable`
      statt epoll/timerfd, im Programm nachgewiesen.
- [x] **systematische Suche nach stillen Plattformweichen**
      (`scripts/find-silent-fallbacks.py`) – ein echter Treffer, zwei Fehlalarme
- [x] **auf Hardware gestartet** – `FlutterEngineInitialize` liefert auf der
      echten Switch `kSuccess`, `FlutterEngineShutdown` läuft sauber durch.
      Damit stehen Shell, Threads und Nachrichtenschleifen.
- [x] **dart:io vollständig** – alle Gruppen der Landkarte abgearbeitet:
      Sockets, Namespace, Console und SSL-Kontext über erweiterte Guards auf
      der POSIX-Ebene; `process_horizon.cc` und
      `file_system_watcher_horizon.cc` als kurze, ehrlich ablehnende Fassungen;
      `crypto_horizon.cc` mit `csrngGetRandomBytes` als Zufallsquelle
- [x] **POSIX-Lücken geschlossen** – `fstatat`, `fchdir`, `open64`, `openat64`,
      `pread`, `pipe`, `pthread_sigmask`, `readlinkat`, `symlinkat`,
      `utimensat` in der Compat-Schicht des Embedders
- [x] **Wurzelzertifikate eingebaut** – `dart_use_fallback_root_certificates`;
      Horizon hat keinen Systemzertifikatspeicher, ohne das Flag wäre HTTPS
      unmöglich
- [x] **offene Symbole: 100 → 30**
- [x] **alle Symbole aufgelöst – die NRO linkt vollständig** (13 MB).
      Nachgewiesen im Programm: alle sechs benutzten Funktionen der
      Embedder-API, 49 `SkTypeface_FreeType`-Symbole,
      `SkFontMgr_New_Custom_Empty`, `absl::…::LowLevelAlloc::Alloc`,
      `dart::Dart::Init`, `fml::MessageLoopHorizon::Run` und
      `dart::bin::root_certificates_pem_length`
- [x] **`FlutterEngineRunInitialized` läuft an** – die Startsequenz kommt bis in
      `EventHandler::Start`; Stackgrenzen, Meldeweg und Weckkanal stehen
      (siehe „Stand der Fehlersuche")

## Meilenstein 3 – Die Engine läuft

**Stand 2026-08-11, im aufgeräumten Baum ohne Debug-Marken bestätigt:** Start,
Frames mit Text, sechs Systemschriften, sauberer Abbau. Keine Instrumentierung
mehr im Weg – insbesondere kein TCP-Log je Heap-Allokation und keine Datei je
Logzeile auf der SD-Karte.

- [x] **`gen_snapshot` aus dem eigenen Checkout gebaut** – 463 Ziele,
      `clang_x64/gen_snapshot_product`, 6,6 MB x86-64-Linux. Der Umweg über die
      Android-Artefakte des SDK trägt nicht mehr, Begründung unten.
- [x] **AOT-Snapshot damit erzeugt** – 3,1 MB, 150.948 Zeilen Assembly
- [x] **`Dart_Initialize` läuft durch** – Versionsprüfung ok, Merkmale
      `… arm64 horizon no-compressed-pointers`
- [x] **Dart AOT lädt**
- [x] **Flutter Engine startet** – `FlutterEngineRunInitialized` = `kSuccess`,
      `SendWindowMetricsEvent` = `kSuccess`, Heap wächst im laufenden Betrieb
- [x] **erster Frame** – der wandernde Balken aus `examples/ui_app/dart/main.dart`
      läuft über den Bildschirm der Konsole. Damit steht die ganze Kette:
      Isolate, `dart:ui`, `PlatformDispatcher`, Szene, Software-Renderer,
      Framebuffer.
- [x] **Text im Bild** – Systemschriften der Konsole über den Dienst `pl:u`,
      eingehängt als `SkFontMgr_New_Custom_Data`. Alle sechs Schnitte
      (Standard, CJK, Koreanisch, Nintendo-Sonderzeichen) werden geladen.
- [x] **sauberes Beenden** – `project.shutdown_dart_vm_when_done = true`.
      Ohne das Feld bleibt `settings.leak_vm` auf seinem Vorgabewert `true`
      (`common/settings.h:278`, ausgewertet in `embedder.cc:2079`): Die Shell
      wird abgebaut, die Dart-VM aber absichtlich stehen gelassen. Dann läuft
      `Dart_Cleanup` nie und damit auch nicht `EventHandler::Stop()`, dessen
      Poll-Thread still in `poll()` auf dem Weckkanal wartet.
      Ohne das Feld bleibt `settings.leak_vm` auf seinem Vorgabewert `true`
      (`common/settings.h:278`, ausgewertet in `embedder.cc:2079`): Die Shell
      wird abgebaut, die Dart-VM aber absichtlich stehen gelassen. Dann läuft
      `Dart_Cleanup` nie und damit auch nicht `EventHandler::Stop()`, dessen
      Poll-Thread still in `poll()` auf dem Weckkanal wartet.
- [x] **Controller-Eingabe wirkt – auf Hardware bestätigt** (2026-08-11):
      Steuerkreuz, linker Stick und A bewegen den Fokus und drücken den
      Knopf. Es lagen zwei Ursachen hintereinander, beide waren stumme
      Verwerfungen: Das View-Fokus-Ereignis wurde zu früh gesendet
      (`runtime_controller.cc:216` verwirft ohne Isolate kommentarlos, der
      Aufruf meldet trotzdem `kSuccess` – jetzt gesendet nach dem ersten
      präsentierten Frame), und reine KeyData-Ereignisse stellt das
      Framework in eine Warteschlange, die erst eine Rohnachricht auf
      `flutter/keyevent` leeren würde (`hardware_keyboard.dart:1088`) –
      gelöst über den Sofort-Dispatch-Pfad für synthetische Ereignisse.
      `handled` in der Rückmeldung ist seitdem prinzipbedingt immer 0 und
      **kein** Diagnosesignal mehr. Einzelheiten in `docs/porting-notes.md`.
- [x] **Sporadischer Absturz (`_malloc_r` / `threadCreate 0xd401`) behoben**
      (2026-08-11): Die NRO erbt den Heap des Wirtsprozesses samt
      Kernel-Seitenzuständen. Nach einem *erfolgreichen* Lauf bleiben per
      `svcMapMemory` „geliehene" Regionen (Stacks abgelöster Threads, die
      libnx nicht mehr abräumt) im Heap zurück; der nächste Start im selben
      Prozess hielt diese unlesbaren Löcher für freien Speicher. Freilisten-
      Kette hinein → Data Abort in `_malloc_r` (`FAR = X3+8`); `memalign`
      reicht ein Loch an `threadCreate` weiter →
      `KernelError_InvalidMemoryState` (0xd401). Ein Absturz riss den
      Prozess mit, der Folgelauf startete sauber – daher „jeder zweite
      Start". **Reparatur: Erbschaftsbereinigung beim Start**
      (`flutter_libnx_heal_heap` in `thread_diag_horizon.cpp`): geerbte
      Löcher werden über `svcUnmapMemory`-Paarung mit größengleichen
      Spiegeln zurückgebaut; der eigene Main-Stack ist ausgenommen, falsche
      Paarungen lehnt der Kernel ab. Auf Hardware bestätigt über drei
      Zyklen im selben Prozess (Erfolg → Bereinigung → Erfolg → …), wo
      vorher jeder zweite Lauf starb.
- [x] **Systemabsturz nach dem dritten Zyklus behoben** (2026-08-11):
      Fehlerbild 2011-0102 (HIPC, „out of sessions") *nach* sauberem
      App-Abgang, reproduzierbar beim jeweils dritten Lauf im selben
      Prozess, in zwei unabhängigen Sitzungen. Ursache: Service-Sessions
      überleben den NRO-Wechsel wie die Heap-Zustände – jeder Lauf ließ
      eine pl:u-Session (`plInitialize` ohne `plExit`, samt gemappter
      Shared-Font-Memory) und das romfs-Storage-Handle zurück, bis das
      Session-Limit erschöpft war und der hbmenu-Reload starb. Reparatur:
      `flutter_libnx_fonts_cleanup()` (plExit) und `romfsExit()` nach dem
      Engine-Shutdown. **Auf Hardware bestätigt: vier Zyklen im selben
      Prozess sauber durchlaufen und beendet** – vorher starb der Prozess
      deterministisch nach dem dritten.
- [x] **`runApp()` mit dem Flutter-Framework** – `MaterialApp`, `Scaffold`,
      `ThemeData`/`ColorScheme`, `AnimationController` am Ticker, `Icons`
      (Icon-Schriftart), `LinearProgressIndicator`, `Card`. Damit läuft der
      vollständige Stapel, nicht mehr nur `dart:ui`.
- [x] **Touch** – Berührungen des Touchscreens werden als
      `FlutterPointerEvent` zugestellt; ein `FilledButton` reagiert und zählt
      hoch. Zustandsverfolgung in `examples/ui_app/source/main.cpp`
      (`SendTouchEvents`), Auslesen in `switch_platform.cpp`.
- [x] Controller – siehe „Controller-Eingabe wirkt" oben
- [x] **Platform Channels** (2026-08-11) – `platform_message_callback` im
      Embedder, MethodChannel `flutter_libnx/system` mit JSONMethodCodec.
      Nachweis auf Hardware: Akku-Knopf in der App fragt den Batteriestand
      ab, der Embedder antwortet über den psm-Dienst, Wert stimmt.
      Wichtig: Mit registriertem Callback muss der Embedder **jede**
      Nachricht mit response_handle beantworten (leere Antwort = „nicht
      implementiert"), sonst hängt das await auf der Dart-Seite für immer.
      Nebenbei repariert: `csrngInitialize`/`csrngExit`-Paar –
      `Random.secure()` hätte bisher EIO geworfen.

## Meilenstein 4 – Allgemeiner Bauweg

- [x] **Ein unverändertes `flutter create`-Projekt läuft auf der Konsole**
      (2026-08-11): `flutter create counter_demo` (Standard-Counter, keine
      einzige Änderung am Projekt) → `build-dart-app.ps1 -Project …`
      (Assets über `flutter build bundle`, Kernel über die Paketauflösung
      des Projekts) → `rebuild-all.sh` → NRO. Auf Hardware bestätigt,
      Touch und Controller wirken. Damit ist der Bauweg projektunabhängig
      belegt, nicht nur am eigenen Beispiel.
- [x] **Erste echte fremde App gebaut und gestartet: `rezkonv_app`**
      (Kochbuch, miri2577; 2026-08-11): Unverändert durch die ganze Kette
      (41-MB-Dill, 2,24 Mio. Zeilen Assembly, 36-MB-NRO, null offene
      Symbole – auch `dart:ffi` linkt). **Auf Hardware: Engine startet,
      erster Frame wird präsentiert, App läuft trotz fehlender Dienste
      weiter** – die leere Kanal-Antwort erzeugt saubere
      `MissingPluginException`s statt hängender awaits.
- [x] **StandardMessageCodec im Embedder** (2026-08-11):
      `embedder/src/engine/standard_message_codec.cpp` – Teilmenge
      null/bool/int/double/String/List/Map, typisierte Arrays lehnen
      ehrlich ab. Damit sprechen die `plugins.flutter.io/*`-Kanäle.
- [x] **`path_provider` und `shared_preferences` implementiert**
      (`embedder/src/engine/plugins_horizon.cpp`): Pfade unter
      `/switch/flutter_apps/<app-id>/` (werden angelegt), Preferences als
      codec-kodierte Datei ebendort. Auf Hardware: beide Exceptions weg,
      rezkonv lädt Einstellungen und kommt bis zur Datenbank.
- [x] **Stille Plattformweiche in `Utils::*DynamicLibrary` beseitigt**
      (Patch `patch_dart_platform_utils_dynlib`): Für Horizon griff in
      Load/Resolve/Unload kein `#if`-Zweig – Durchfallen ohne `return`,
      undefiniertes Verhalten, auf Hardware Data Abort (FAR=0x48) in
      `DN_Ffi_dl_providesSymbol`, sobald `package:sqlite3` den Prozess
      abfragte. Jetzt: Fehlertext + nullptr → saubere Dart-Exception.
      **Auf Hardware: rezkonv bleibt offen und bedienbar**, die App zeigt
      den DB-Fehler in ihrer eigenen Fehlerfläche an.
- [x] **sqlite3 via FFI – vollständig, auf Hardware bestätigt**
      (2026-08-11 Nacht): rezkonv läuft fehlerfrei mit Datenbank und
      FTS5-Volltextsuche. Die Kette (alles projektunabhängig):
      * Amalgamation 3.53.4 selbst gebaut (`scripts/build-sqlite3.sh`,
        devkitPro führt kein sqlite-Portlib), Flags dokumentiert;
        FTS5 + Math + R-Tree aktiviert
      * dl-Haken in der Engine (`patch_dart_platform_utils_dynlib_hooks`,
        `patch_dart_ffi_dynamic_library_hooks`): `DynamicLibrary.open`/
        Symbolauflösung fragen den Embedder; ohne ihn ehrliche Fehler
      * Symboltabellen im Embedder (`static_libraries_horizon.cpp`):
        278 sqlite3-Symbole (T **und** Daten – `sqlite3_temp_directory`
        ist eine Variable!) + Prozess-Symbole (malloc/free/calloc/realloc
        für package:ffi)
      * `unix-none` (ohne Dateisperren) als Standard-VFS – devoptab kennt
        keine fcntl-Locks, ein Zweitprozess existiert nicht
      * **Zwei devoptab-Fallen in der Compat-Schicht behoben**
        (`--wrap=read`, `--wrap=stat`): Lesen ab EOF lieferte -1 statt 0;
        `stat()` auf eine gerade geöffnete Datei scheitert mit EIO (auch
        ein Zweit-`open` – exklusiv gehalten). ENOENT vs. EIO
        unterscheidet Nicht-Existenz von Belegt; im Belegt-Fall kommt
        eine ehrliche Minimalantwort (S_IFREG|0644).
      * C-Rauchprobe `sqlite3_probe_horizon.cpp` (open→CREATE→INSERT→
        SELECT=42) läuft bei jedem Start – nach der Stabilisierung
        hinter ein Flag legen oder ausbauen
- [x] **Bildschirmtastatur – auf Hardware bestätigt** (2026-08-11 Nacht):
      `textinput_horizon.cpp` bedient `flutter/textinput` (JSON-Codec);
      `TextInput.show` öffnet das swkbd-Systemapplet (blockierend, das
      Applet übernimmt den Bildschirm ohnehin), Bestätigen geht als
      `TextInputClient.updateEditingState` + `performAction` zurück.
      Auswahl-Indizes in UTF-16-Einheiten (Flutter zählt so, swkbd
      liefert UTF-8). **Rezept wurde damit angelegt, gespeichert und hat
      den Neustart überlebt – der volle App-Zyklus funktioniert.**
- [x] **`file_picker`** – Switch-Konvention statt Datei-Browser: „Datei
      wählen" liefert den Inhalt von
      `/switch/flutter_apps/<app-id>/import/` (Einzelauswahl: neueste
      Datei; Mehrfach: alle passenden; leer: null = abgebrochen).
      Kanal auf Hardware bestätigt; Volltest braucht eine Importdatei.
- [x] **`url_launcher` – auf Hardware bestätigt** (2026-08-15): öffnet
      http/https im Systembrowser der Konsole (NetFront-Web-Applet,
      `webPageCreate` + `webConfigShow`; Domain wird automatisch
      freigeschaltet). Antwort geht vor dem blockierenden Applet raus,
      wie beim swkbd. Nur-http/https ehrlich über `canLaunch`.
- [ ] Frame-Messung eingebaut (600-Frame-Fenster in `SoftwarePresent`),
      liefert aber erst bei anhaltendem Zeichnen (Scrollen/Animation)
      eine Zeile – Zahlen stehen noch aus.
- [ ] rezkonv-Rest: `LateInitializationError` eines App-Singletons
      (hängt mutmaßlich am Secure-Storage-Stub) – beobachten.
- [ ] sqlite3-Rauchprobe ist stillgelegt (Aufruf auskommentiert in
      `main.cpp`), bleibt für künftige Datei-I/O-Fehlersuchen reaktivierbar.
- [ ] Referenz-Brocken bleibt Referenz-App/Referenz-App (dem privaten App-Repo;
      maßgeblicher Stand: `Desktop\Referenz-App-Arbeitsstand`) – nach Plan erst UI/
      Netzwerk, Video und WebView bleiben Forschungsprojekte
      (`docs/target-apps.md`).

## Meilenstein 5 – GPU-Rendering und der Referenz-App-Feldtest

- [x] **Skia rendert über OpenGL (Mesa/nouveau)** – 31–37 fps im Zeichnen
      statt 12–15 in Software, auf Hardware gemessen. EGL-Kontext im
      Embedder (`egl_horizon.cpp`), Laufzeit-Weiche mit
      Software-Rückfall, Impeller herauskonfiguriert.
- [x] **Virtuelle Dateihandles** – der Dateidienst hält Dateien exklusiv;
      pro Pfad ein echter Deskriptor, jedes `open()` ein virtuelles Handle
      mit eigenem Offset. Damit läuft Hive (Doppel-Open je Box) und jede
      künftige Bibliothek mit demselben Muster. fcntl-Sperren trivial
      erfüllt.
- [x] **Wurzelzertifikate auf Horizon** – `TrustBuiltinRoots` ließ den
      Rückfall auf die einkompilierten Zertifikate hinter
      `DART_HOST_OS_LINUX` liegen; Patch erweitert den Guard. TLS/DoH
      funktionieren seitdem.
- [x] **Socket-Konfiguration für echte Last** – libnx-Defaults (3
      Sessions, kleine Puffer) brachen unter Referenz-App' Paralellität mit
      ENOBUFS zusammen; jetzt 16 Sessions, 128/512-KB-Puffer.
- [x] **Referenz-App läuft auf der Konsole:** UI auf der GPU, Lokalmodus,
      Login, **WebDAV-Cloud-Anmeldung erfolgreich und funktional**. Drei
      dokumentierte App-Guards (MediaKit/windowManager/IntroPage) im
      Referenz-App-Repo. Video bleibt Forschungsprojekt.
- [x] **Die Video-Brücke: media_kit liefert Bild und Ton** (2026-08-16,
      auf Hardware über viele Zyklen bestätigt): Kanal
      `com.alexmercerind/media_kit_video` im Embedder
      (`media_kit_video_horizon.cpp`), mpv rendert **direkt in Skias
      Fensterkontext** im Textur-Frame-Callback auf dem Raster-Thread
      (nouveau vertraegt weder nebenlaeufige Submission noch
      Textur-Sharing zwischen Kontexten), Render-Kontext eager per
      `FlutterEnginePostRenderThreadTask` (ohne ihn waehlt mpv keine
      Videospur), `flip_y=0` empirisch, Audio ueber mpvs SDL-Ausgabe.
      Sechs Befunde in `docs/porting-notes.md` (III).
- [x] **fsync auf virtuellen Handles gewrappt** – EBADF brach media_kits
      player.open; devoptab-Toleranz in der Compat-Schicht.
- [x] **Log-Rotation** – `ui_app.log` wird zu `.alt` rotiert statt
      ueberschrieben; nach einem harten Konsolen-Absturz ist die Datei
      die einzige Quelle des Berichts.
- [x] **Referenz-App-Feldtest Video**: Intro und Cloud-Filme (WebDAV-Cloud, ueber
      app-seitigen TLS-Proxy) laufen mit Bild und Ton. App-seitige
      Switch-Anpassungen leben im eigenen Repo **Referenz-App-Switch-Repo**
      (lokaler TLS-Proxy + HLS-Umschreibung, Resolver-DoH,
      Relay-Route) – FFmpeg des Portlibs hat kein TLS,
      Horizon keine Kindprozesse (kein curl-Fallback).
- [x] **Echte App Ende-zu-Ende auf Hardware (2026-08-16):** Referenz-App spielt
      einen HLS-Stream komplett — Liste → Detail →
      Hoster-Resolver → HLS → Player mit Bild und Ton, **ohne Server**.
      Cover werden angezeigt. Kein Relay/curl/WebView nötig: Darts TLS wird
      direkt akzeptiert, eigener GET über DoH-IP + SNI. App-seitiger
      Integrationsweg in `docs/flutter-app-integration.md` +
      Kopiervorlagen `dart_helpers/` (für JEDE Flutter-App).
- [x] **Player-Teardown-Absturz behoben:** `player.dispose()` →
      `mpv_terminate_destroy` korrumpiert beim Thread-Abbau den Heap
      (Horizon-Stack-Erbschaft, intra-session). Fix: persistente
      Player-Instanz, beim Verlassen nur `stop()`. Details in
      porting-notes (IV).
- [ ] WebView-abhängige Hoster (WebView-abhaengige) — regex/JS
      ohne WebView nachbauen, falls gewünscht (Regex-Resolver laufen bereits).
- [ ] Ein harter Konsolen-Absturz beim Video-Start (vor der
      Ein-Kontext-Architektur) blieb unerklaert – Log brach in
      Binaermuell ab, kein Handler lief. Beobachten.
- [ ] `package_info` (Kanal dev.fluttercommunity.plus/package_info) –
      kleiner Baustein, Werte aus dem Build.
- [ ] Aufräumen: doppelter is_horizon-Block in flutter/skia/BUILD.gn
      (der spätere gewinnt; bei frischem Checkout glätten).
- [ ] gen_kernel laeuft ohne `-Ddart.vm.product=true` – kDebugMode ist
      im AOT-Build wahr, Debug-Pfade (media_kit NativeReferenceHolder)
      laufen mit. Eigener Zyklus.

## Fehlersuche vom 2026-08-10 – abgeschlossen

**Ergebnis vorweg:** Vier Ursachen lagen hintereinander, jede verdeckte die
nächste. Am Ende lief die Engine. Der Weg dorthin ist unten der Reihe nach
festgehalten, weil drei der vier Befunde dasselbe Muster haben – *ein
Meldeweg, der nicht angeschlossen war*.

Der Absturz saß in `FlutterEngineRunInitialized`. Zwei Läufe mit
Instrumentierung haben ihn zunächst eingegrenzt:

- **Die Dart-VM gibt keine eigene Meldung aus.** `OS::Print`/`OS::PrintErr`
  gehen über eine schwach gebundene Funktion an die TCP-Senke; es kommt
  nichts. Der Fatal-Screen der Konsole zeigt `2168-0002`, Data Abort.
- **`VirtualMemory::AllocateAligned` wird nie erreicht.** Auch dort meldet
  jeder Aufruf Name, Größe, Ausrichtung und Flags – keine einzige Zeile
  kommt an. Der Absturz liegt damit *vor* der ersten Heap-Anforderung.

Kein Deadlock: Die Konsole schließt die Verbindung, statt sie offen zu halten.

**Der Verdacht auf `GetCurrentStackBounds` hat sich in der Quelle bestätigt**
(Belege im Einzelnen in `docs/porting-notes.md`):

- `vm/os_thread.cc:50` macht aus einem `false` **`FATAL("Failed to retrieve
  stack bounds")`**. Der bisherige Vermerk, die VM falle auf konservativere
  Verfahren zurück, war falsch – er beschreibt eine ältere Dart-Fassung.
- Der Aufruf steht im Konstruktor jedes `OSThread`, und der erste entsteht in
  `OSThread::Init()` (`vm/dart.cc:374`), also mitten in `Dart_Initialize`.
- `Dart_Initialize` läuft **nicht** bei `FlutterEngineInitialize`, sondern erst
  bei `FlutterEngineRunInitialized`: Dort ist `EmbedderEngine::LaunchShell()`
  der erste Schritt (`embedder.cc:2476`), und erst der ruft `Shell::Create` →
  `DartVMRef::Create` → `Dart_Initialize`. Damit ist der FATAL das Erste, was
  in `RunInitialized` überhaupt passieren kann.

**Warum die Meldung fehlte, war ein zweiter Fehler.** `FATAL` schreibt über
`Syslog::PrintErr` (`platform/assert.cc:37`), nicht über `OS::PrintErr` – und
`syslog_linux.cc` schreibt auf `stderr`, das auf der Konsole nirgendwohin geht.
Die ausbleibende Meldung war also **kein** Beleg dafür, dass die VM nichts zu
melden hatte.

Beides ist behoben und **auf Hardware bestätigt**:

- [x] `Syslog::VPrint`/`VPrintErr` gehen für Horizon durch dieselbe schwach
      gebundene Senke wie `OS::Print` (Patch in `patch-engine-horizon.py`)
- [x] `GetCurrentStackBounds` liefert echte Grenzen – über `svcQueryMemory` auf
      den aktuellen Stackpointer, gestellt vom Embedder
      (`embedder/src/platform/stack_bounds_horizon.cpp`), damit `<switch.h>`
      nicht in die Dart-VM gerät

### Was der erste Lauf mit Meldeweg zeigte

Sofort eine verwertbare Zeile – `socketpair` schlägt mit `ENOSYS` fehl, weil
libnx es nur pro forma definiert. Ersetzt durch ein selbst geknüpftes
TCP-Paar über `127.0.0.1`, mit einem Rückfall ohne Weckdeskriptor. Der
Hardware-Lauf hat entschieden: **Loopback trägt**, der Rückfall bleibt
ungenutzt. Einzelheiten in `docs/porting-notes.md`.

### Die dritte Ursache: ein weiterer stummer Kanal

Danach brach es erneut ohne Meldung ab. Sieben Marken zwischen
`EventHandler::Start` und `OS::Init` haben die Stelle eingekreist: Es lag
**nicht** am Poll-Thread und nicht an `SocketBase::Initialize` – beide laufen
durch – sondern in `SnapshotHeaderReader::InitializeGlobalVMFlagsFromSnapshot`.

Und auch das war kein Speicherfehler, sondern ein sauber erkannter Fehler in
einem dritten nicht angeschlossenen Kanal: `FML_LOG` schreibt in
`fml/logging.cc` mit `fprintf` nach `stderr` und ruft für `kLogFatal`
anschließend `KillProcess()` → `abort()`. Die Engine hat den Fehler die ganze
Zeit gemeldet.

- [x] `FML_LOG` geht für Horizon durch dieselbe Senke. Anders als die Marken in
      `dart.cc` ist das kein Wegwerf-Code: Ohne diesen Weg bleibt jede
      Engine-Meldung auf der Zielplattform unsichtbar.

### Die vierte Ursache: der Snapshot passte nicht zur VM

```text
Wrong full snapshot version, expected '0150713ccc165a92bb03706c55150060'
                            found    '78da37fed6bf1489361a312568249f3f'
```

`dart/tools/make_version.py:20-45` bildet den Snapshot-Hash als MD5 über 15
Quelldateien, darunter `dart.cc`, `app_snapshot.cc` und `image_snapshot.cc`.
**Jede Änderung an einer davon macht jeden vorhandenen Snapshot ungültig** –
und die Portierung ändert mehrere davon. Dazu kommt ein zweiter, härterer
Grund: Wir bauen mit `dart_use_compressed_pointers=false`, der
Android-`gen_snapshot` erzeugt Snapshots *mit* Compressed Pointers.

Damit ist der Shortcut aus Meilenstein 1b erledigt. Er trug für den Ladeweg,
nicht für die Ausführung.

**Arbeitsregel ab jetzt:** `gen_snapshot` und Engine kommen immer aus
demselben Stand. Wer eine der 15 Dateien anfasst – auch nur, um eine
Debug-Marke zu entfernen –, muss beides neu bauen.

### Ergebnis

```text
SnapshotFlags: VerifyVersion durch (ok)
SnapshotFlags: Merkmale = '… arm64 horizon no-compressed-pointers'
Dart::Init: OSThread::Init fertig (Stackgrenzen stehen)
VirtualMemory::AllocateAligned name=dart-heap size=524288 → 0x5755080000
FlutterEngineRunInitialized = 0 (kSuccess)
SendWindowMetricsEvent     = 0 (kSuccess)
```

Auf dem Bildschirm der Konsole läuft der blaue Balken aus `main.dart`.

### Zur Fehlermeldung der Konsole

Der Code lautet **2162-0002**, nicht `2168-0002`. Er ist der generische Fatal
für eine unbehandelte Ausnahme im Programm und benennt keine Ursache – bei drei
der vier Befunde war er die Folge eines `abort()` nach einer korrekt erkannten
Fehlersituation. Die Adressen des Backtrace ließen sich gegen `ui_app.elf`
nicht schlüssig auflösen; die dort genannte Startadresse ist nicht die Basis
unserer NRO. Der Fatal-Screen ist kein Werkzeug. Die Logsenke ist es – aber nur,
solange jeder Kanal daran hängt.
