# flutter-libnx

**Run unmodified Flutter apps on the Nintendo Switch (homebrew).**
**Unveränderte Flutter-Apps auf der Nintendo Switch (Homebrew).**

[English](#english) · [Deutsch](#deutsch)

![status](https://img.shields.io/badge/status-experimental-orange)
![platform](https://img.shields.io/badge/platform-Horizon%20OS%20(libnx)-red)
![flutter](https://img.shields.io/badge/Flutter-3.41.6-blue)

---

## English

### What is this?

flutter-libnx is a port of the **Flutter engine** to Horizon OS, the operating
system of the Nintendo Switch, built on the homebrew toolchain
([devkitPro](https://devkitpro.org/)/libnx). The goal is a **general-purpose
tool**: take an *unmodified* Flutter project, run one build script, get a
single `.nro` homebrew file that runs on the console.

This is not a proof of concept that draws a triangle. A real, unmodified
everyday app (a recipe manager with database, full-text search, file import
and system keyboard) runs, is fully usable, and survives restarts.

### What works today

| Area | Status |
|---|---|
| Flutter framework (`runApp`, Material, animations, fonts) | ✅ on hardware |
| Touch input | ✅ |
| Controller input (D-pad/stick moves focus, A activates) | ✅ |
| On-screen keyboard (system swkbd applet) | ✅ |
| Dart AOT (release mode, no JIT needed) | ✅ |
| `dart:io` — files, sockets, TLS/HTTPS | ✅ |
| sqlite3 via `dart:ffi` (incl. FTS5) — `drift`, `sqflite_common_ffi` | ✅ |
| Plugins: `path_provider`, `shared_preferences`, `url_launcher` (system browser), `file_picker` (fixed import folder), `flutter_secure_storage` (plaintext, documented) | ✅ |
| Platform channels (JSON + standard binary codec) | ✅ |
| Rendering | Software renderer (12–15 fps under load); **GPU rendering via Mesa/nouveau in progress** |
| Clean exit, repeated launches in the same host process | ✅ |

### How it works (short version)

* The **Flutter engine** (pinned: Flutter 3.41.6) is cross-compiled with
  devkitA64 as a static library. `horizon` was added as a target OS to GN,
  fml, the Dart VM (five platform files: threads, virtual memory, cpuinfo …)
  and `dart:io` (poll-based event handler, loopback wake channel).
* The **embedder** (this repo) provides everything the engine expects from a
  platform: framebuffer or EGL surface, input, task runners, fonts from the
  console's `pl:u` service, message loop, POSIX compatibility layer.
* **No `dlopen` exists on Horizon.** FFI works anyway: native libraries
  (e.g. sqlite3) are statically linked and resolved through an embedder hook
  with generated symbol tables — `DynamicLibrary.open('libsqlite3.so')`
  just works, apps don't notice.
* **Plugins** are implemented in the embedder against their real method
  channels, so unmodified pub.dev packages work. A generated "Horizon
  registrant" replaces the plugin registrant that `flutter build` would
  normally create.

The full engineering journal — every bug, every dead end, every fix, with
file/line evidence — lives in [`docs/status.md`](docs/status.md) and
[`docs/porting-notes.md`](docs/porting-notes.md) (German).

### Requirements

* A Nintendo Switch capable of running homebrew (Atmosphère), **launched in
  application mode** (hold R while starting a game → hbmenu). Applet mode has
  only ~380 MB of RAM and will not run the framework.
* Windows with WSL2 (the build chain uses both), ~40 GB disk for the engine
  checkout, [devkitPro](https://devkitpro.org/) (a container image works —
  no root needed).
* Flutter SDK **3.41.6** (the engine version is pinned to its commit).

### Building

The chain, end to end:

```text
1. Engine checkout (gclient sync, ~26 GB, once)
2. python3 scripts/patch-engine-horizon.py     # all Horizon patches, idempotent
3. scripts/gn-gen-horizon.sh                   # GN config for horizon/arm64
4. scripts/build-horizon.sh ...:flutter_engine_static
5. bash scripts/build-sqlite3.sh               # sqlite3 + symbol table
6. scripts/build-dart-app.ps1 -Project <your flutter project>
                                               # assets via `flutter build bundle`,
                                               # kernel, Horizon registrant
7. wsl bash scripts/rebuild-all.sh ui_app      # AOT snapshot -> .nro
8. scripts/nxlink-upload.ps1 -SwitchIp <ip> -Example ui_app
```

Steps 1–5 are one-time (per engine update). After that, iterating on an app
is steps 6–7. `scripts/log-listener.ps1` receives the console's log over TCP
(the console connects to your PC, so NAT/firewalls don't get in the way).

### Bringing your own app

```powershell
scripts/build-dart-app.ps1 -Project C:\path\to\your_flutter_app
wsl bash /mnt/e/flutter-libnx/scripts/rebuild-all.sh ui_app
```

That's it — no changes to the app. Assets, fonts and manifests come from
`flutter build bundle`; the packages your app uses are resolved through its
own `package_config.json`. App data lives under
`/switch/flutter_apps/<app-id>/` on the SD card; files to import go into
the `import/` subfolder (there is no on-console file browser yet — the
picker offers that folder's contents).

### Known limitations

* Software rendering caps at ~12–15 fps for full-screen redraws; the GPU
  backend (OpenGL via switch-mesa/nouveau) is being brought up.
* `flutter_secure_storage` stores **plaintext** (FAT has no keychain) — fine
  for homebrew, not for real secrets. Documented in the code.
* Video playback and WebView are out of scope for now (see
  `docs/target-apps.md` for the analysis).
* One display resolution (1280×720); docked scaling not yet implemented.

### Legal

This is an independent open-source project for **homebrew development on
hardware you own**. It is not affiliated with, endorsed by, or supported by
Nintendo or Google. Nintendo Switch is a trademark of Nintendo. No part of
this project enables or condones piracy.

Licenses: this repository is MIT (see [LICENSE](LICENSE)). It builds against
the Flutter engine (BSD-3-Clause, patches in `patches/`), bundles the SQLite
amalgamation (public domain) and one reference header from Flutter
(`third_party/reference/embedder.h`, BSD-3-Clause).

---

## Deutsch

### Was ist das?

flutter-libnx ist eine Portierung der **Flutter-Engine** auf Horizon OS, das
Betriebssystem der Nintendo Switch, aufgebaut auf der Homebrew-Toolchain
([devkitPro](https://devkitpro.org/)/libnx). Das Ziel ist ein **allgemeines
Werkzeug**: ein *unverändertes* Flutter-Projekt nehmen, ein Build-Skript
laufen lassen, eine einzelne `.nro`-Homebrew-Datei bekommen, die auf der
Konsole läuft.

Das ist kein Machbarkeitsnachweis, der ein Dreieck zeichnet. Eine echte,
unveränderte Alltags-App (eine Rezeptverwaltung mit Datenbank,
Volltextsuche, Dateiimport und Systemtastatur) läuft, ist voll bedienbar
und übersteht Neustarts.

### Was heute funktioniert

| Bereich | Stand |
|---|---|
| Flutter-Framework (`runApp`, Material, Animationen, Schriften) | ✅ auf Hardware |
| Touch-Eingabe | ✅ |
| Controller (Steuerkreuz/Stick bewegt Fokus, A aktiviert) | ✅ |
| Bildschirmtastatur (swkbd-Systemapplet) | ✅ |
| Dart AOT (Release, kein JIT nötig) | ✅ |
| `dart:io` — Dateien, Sockets, TLS/HTTPS | ✅ |
| sqlite3 über `dart:ffi` (inkl. FTS5) — `drift`, `sqflite_common_ffi` | ✅ |
| Plugins: `path_provider`, `shared_preferences`, `url_launcher` (Systembrowser), `file_picker` (fester Import-Ordner), `flutter_secure_storage` (Klartext, dokumentiert) | ✅ |
| Platform Channels (JSON- und Standard-Binär-Codec) | ✅ |
| Rendering | Software-Renderer (12–15 fps unter Last); **GPU über Mesa/nouveau in Arbeit** |
| Sauberes Beenden, wiederholte Starts im selben Wirtsprozess | ✅ |

### Wie es funktioniert (Kurzfassung)

* Die **Flutter-Engine** (gepinnt: Flutter 3.41.6) wird mit devkitA64 als
  statische Bibliothek cross-kompiliert. `horizon` wurde als Ziel-OS in GN,
  fml, der Dart-VM (fünf Plattformdateien: Threads, virtueller Speicher,
  cpuinfo …) und `dart:io` ergänzt (poll-basierter Eventhandler,
  Loopback-Weckkanal).
* Der **Embedder** (dieses Repo) stellt alles, was die Engine von einer
  Plattform erwartet: Framebuffer bzw. EGL-Oberfläche, Eingabe, Task-Runner,
  Systemschriften über den `pl:u`-Dienst, Nachrichtenschleife,
  POSIX-Kompatibilitätsschicht.
* **Horizon hat kein `dlopen`.** FFI funktioniert trotzdem: Native
  Bibliotheken (z. B. sqlite3) werden statisch gelinkt und über einen
  Embedder-Haken mit generierten Symboltabellen aufgelöst —
  `DynamicLibrary.open('libsqlite3.so')` funktioniert einfach, Apps merken
  nichts davon.
* **Plugins** sind im Embedder gegen ihre echten Method-Channels
  implementiert, unveränderte pub.dev-Pakete funktionieren daher. Ein
  generierter „Horizon-Registrant" ersetzt den Plugin-Registrant, den
  `flutter build` normalerweise erzeugt.

Das vollständige Ingenieurs-Tagebuch — jeder Fehler, jede Sackgasse, jede
Reparatur, mit Datei-/Zeilenbelegen — steht in
[`docs/status.md`](docs/status.md) und
[`docs/porting-notes.md`](docs/porting-notes.md).

### Voraussetzungen

* Eine homebrew-fähige Nintendo Switch (Atmosphère), **im Anwendungsmodus
  gestartet** (R gedrückt halten beim Spielstart → hbmenu). Der Applet-Modus
  hat nur ~380 MB RAM — das Framework startet dort nicht.
* Windows mit WSL2 (die Baukette nutzt beides), ~40 GB Platz für den
  Engine-Checkout, [devkitPro](https://devkitpro.org/) (Container-Image
  genügt — kein root nötig).
* Flutter-SDK **3.41.6** (die Engine-Version ist auf dessen Commit gepinnt).

### Bauen

Die Kette, von vorn bis hinten:

```text
1. Engine-Checkout (gclient sync, ~26 GB, einmalig)
2. python3 scripts/patch-engine-horizon.py     # alle Horizon-Patches, idempotent
3. scripts/gn-gen-horizon.sh                   # GN-Konfiguration horizon/arm64
4. scripts/build-horizon.sh ...:flutter_engine_static
5. bash scripts/build-sqlite3.sh               # sqlite3 + Symboltabelle
6. scripts/build-dart-app.ps1 -Project <dein Flutter-Projekt>
                                               # Assets via `flutter build bundle`,
                                               # Kernel, Horizon-Registrant
7. wsl bash scripts/rebuild-all.sh ui_app      # AOT-Snapshot -> .nro
8. scripts/nxlink-upload.ps1 -SwitchIp <ip> -Example ui_app
```

Schritte 1–5 sind einmalig (je Engine-Stand). Danach besteht die Iteration
an einer App aus den Schritten 6–7. `scripts/log-listener.ps1` empfängt das
Konsolen-Log über TCP (die Konsole verbindet sich zum PC — NAT und Firewall
stehen so nicht im Weg).

### Eigene App mitbringen

```powershell
scripts/build-dart-app.ps1 -Project C:\pfad\zu\deiner_flutter_app
wsl bash /mnt/e/flutter-libnx/scripts/rebuild-all.sh ui_app
```

Das ist alles — keine Änderung an der App. Assets, Schriften und Manifeste
kommen aus `flutter build bundle`; die Pakete der App werden über deren
eigene `package_config.json` aufgelöst. App-Daten liegen unter
`/switch/flutter_apps/<app-id>/` auf der SD-Karte; zu importierende Dateien
gehören in den Unterordner `import/` (einen Datei-Browser auf der Konsole
gibt es noch nicht — die Dateiauswahl bietet den Inhalt dieses Ordners an).

### Bekannte Grenzen

* Software-Rendering erreicht bei Vollbild-Neuzeichnung ~12–15 fps; das
  GPU-Backend (OpenGL über switch-mesa/nouveau) ist in Arbeit.
* `flutter_secure_storage` speichert **Klartext** (FAT hat keinen
  Schlüsselbund) — für Homebrew in Ordnung, nicht für echte Geheimnisse.
  Im Code dokumentiert.
* Videowiedergabe und WebView sind vorerst außen vor (Analyse in
  `docs/target-apps.md`).
* Eine Auflösung (1280×720); Docked-Skalierung noch nicht umgesetzt.

### Rechtliches

Ein unabhängiges Open-Source-Projekt für **Homebrew-Entwicklung auf eigener
Hardware**. Es steht in keiner Verbindung zu Nintendo oder Google und wird
von diesen weder unterstützt noch gebilligt. Nintendo Switch ist eine Marke
von Nintendo. Nichts an diesem Projekt ermöglicht oder billigt Piraterie.

Lizenzen: Dieses Repository steht unter MIT (siehe [LICENSE](LICENSE)). Es
baut gegen die Flutter-Engine (BSD-3-Clause, Patches in `patches/`),
bündelt die SQLite-Amalgamation (Public Domain) und einen Referenz-Header
aus Flutter (`third_party/reference/embedder.h`, BSD-3-Clause).
