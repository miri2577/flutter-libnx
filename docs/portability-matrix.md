# Portability Matrix

Stand: 2026-08-09.

Status-Legende:
`OK` = geprüft und vorhanden ·
`WAHRSCH.` = plausibel, aber nicht praktisch verifiziert ·
`LÜCKE` = fehlt, Ersatz nötig ·
`OFFEN` = noch nicht untersucht

| Bereich | Flutter/Dart erwartet | libnx / devkitA64 bietet | Status | Lösung |
|---|---|---|---|---|
| Threads | pthreads (`fml`), eigene Thread-Abstraktion (Dart `os_thread_*`) | Newlib-pthreads über libnx-`Thread` (`nx/source/runtime/newlib.c`), `thread.h` | OK | POSIX-Weg nutzen; Dart braucht neues `os_thread_horizon.{cc,h}` |
| Mutex | `pthread_mutex` | `mutex.h` + Newlib | OK | direkt |
| Condition Variables | `pthread_cond` | `condvar.h` + Newlib | OK | direkt |
| RW-Locks / Semaphore / Barrier | teilweise | `rwlock.h`, `semaphore.h`, `barrier.h` | OK | direkt |
| TLS | `pthread_key_*` / `thread_local` | Newlib TLS, libnx Thread Vars | WAHRSCH. | früh mit Testprogramm prüfen |
| Thread-Stacks | frei wählbar | 4K-Alignment erzwungen (`newlib.c:166`) | OK | Stackgrößen zentral konfigurieren |
| CPU-Cores | mehrere Threads parallel | Homebrew regulär 3 nutzbare Cores | OK | Task-Runner-Zuordnung dokumentieren |
| Filesystem | `open/read/write/stat`, Verzeichnisse | Newlib + devoptab (`sdmc:/`), `romfs` | WAHRSCH. | Assets von SD; `AssetResolver` kapselt Pfade |
| `mmap` / Virtual Memory | Dart `virtual_memory_posix.cc`: `mmap`, `mprotect`, `munmap` | kein `mmap`; `virtmem.h`, `svcMapMemory`, `svcSetMemoryPermission` | **LÜCKE** | `virtual_memory_horizon.cc` neu; Umfang hängt an Abschnitt 4 der Machbarkeitsanalyse |
| Executable Memory | im AOT-Product-Mode hoffentlich nicht nötig | `jit.h` (RW/RX-Doppelmapping) als Rückfall | WAHRSCH. nicht nötig | AOT-Instruktionen in `.text` der NRO linken |
| Dynamic Libraries | `dlopen` für ELF-AOT-Weg und `fml::NativeLibrary` | kein `dlopen` | **LÜCKE** | Symbol-Referenz-Weg der Embedder-API nutzen (`embedder.h:2511-2542`); `native_library_horizon.cc` als No-Op |
| Clock / Timer | `clock_gettime(MONOTONIC)`, hochauflösende Timer | libnx Tick-APIs, Newlib-Clock | WAHRSCH. | Auflösung messen, bevor Task Runner gebaut wird |
| Environment Variables | `getenv` (Engine-Switches) | begrenzt / leer | WAHRSCH. unkritisch | Konfiguration über eigene Config-Struktur |
| Sockets | Dart `HttpClient` | BSD-Sockets über `bsd`-Service | OFFEN | erst ab Meilenstein 13 |
| TLS/HTTPS | BoringSSL (in Engine-DEPS vorhanden) | – | OFFEN | BoringSSL mitbauen, nicht selbst implementieren |
| Locale | ICU-Locale-Daten | keine | OFFEN | ICU-Daten als Asset ausliefern |
| ICU | `icudtl.dat` bzw. eingebettet | – | OFFEN | Größe messen, Datei neben NRO ablegen |
| Fonts | Font-Assets + Skia FontMgr | keine Systemfonts nutzbar | OFFEN | Fonts mit der App bündeln (`FontManifest`) |
| Skia | Software-Backend (`SkSurface` raster) | – | WAHRSCH. | Skia mit minimaler Konfiguration cross-kompilieren |
| Impeller | Vulkan/GLES | devkitPro-Portlibs (Mesa/deko3d) existieren | OFFEN | ausdrücklich **nicht** im MVP |
| Display | RGBA-Framebuffer präsentieren | `display/framebuffer.h`, 1280×720 handheld | OK | `SoftwareSurfacePresentCallback` → Framebuffer-Copy |
| Touch | Pointer-Events | `hidTouchScreen` | WAHRSCH. | Meilenstein 7 |
| Controller | Key-Events / Platform Channel | `runtime/pad.h` (`padUpdate` usw.) | OK | Meilenstein 8 |
| Audio | – | `audio/` in libnx vorhanden | OFFEN | nicht im MVP |
| Clipboard | Platform Channel | – | LÜCKE | nicht im MVP, Channel-Antwort stubben und loggen |
| Lifecycle | App-Lifecycle-Messages | `appletMainLoop`, Applet-Nachrichten | WAHRSCH. | Meilenstein 1 legt die Schleife an |
| Logging | `FlutterLogMessageCallback` (`embedder.h:2448`) | Konsole, `nxlink.h`, Datei auf SD | OK | zentrales Logging-System, Meilenstein 1 |
| Exceptions / RTTI | Engine nutzt beides teilweise | devkitA64 unterstützt beides | WAHRSCH. | Build-Flags früh festlegen |
