# Architektur

## Schichten

```text
Flutter/Dart App  (unverändertes Flutter-Projekt)
  ↓  kompiliert zu
Dart AOT Snapshot (als AArch64-Assembly, in die NRO gelinkt) + flutter_assets (auf SD)
  ↓
Flutter Engine (Dart VM + Skia Raster + Shell), cross-kompiliert für current_os = "horizon"
  ↓  C API
Flutter Embedder API (embedder.h, gepinnt 3.41.6)
  ↓
flutter_libnx_embedder   ← dieses Repo
  ↓
libnx / devkitA64
  ↓
Nintendo Switch / Horizon OS
```

## Aufteilung der Artefakte

| Artefakt | wo gebaut | wie oft |
|---|---|---|
| Flutter Engine (statische Libs) | Build-Host, GN/Ninja | selten – Ergebnis wird als Prebuilt behandelt |
| Embedder | devkitA64 Makefile | bei jeder Änderung am Embedder |
| Dart AOT Snapshot der App | Host, `gen_snapshot` | bei jeder App-Änderung |
| `flutter_assets` | Host, Flutter-Tooling | bei jeder App-Änderung |
| `.nro` | devkitA64 `elf2nro` | Packaging |

Wichtig: ein App-Build darf die Engine **nicht** neu bauen.

## Warum AOT-Assembly statt ELF-Snapshot

Die Embedder-API kennt für `FlutterEngineCreateAOTData` nur `elf_path`, was einen
ELF-Loader (`dlopen`) voraussetzt – auf Horizon-Homebrew nicht verfügbar.
Stattdessen nutzen wir die Snapshot-Felder in `FlutterProjectArgs`, die Symbolreferenzen
akzeptieren. Der Snapshot wird als Assembly erzeugt, mit devkitA64 assembliert und in die
`.nro` gelinkt; die Instruktionen liegen damit in der ohnehin ausführbaren `.text`.

Details und Belege: `docs/feasibility.md`, Abschnitt 1.

## Modulschnitt im Embedder

```text
embedder/
  include/            öffentliche Header
  src/
    flutter_host.{h,cpp}        Engine-Lebenszyklus, Embedder-API-Aufrufe
    log.{h,cpp}                 zentrales Logging (Konsole / nxlink / Datei)
    platform/
      switch_platform.{h,cpp}   Applet-Loop, Framebuffer, Shutdown
      task_runner.{h,cpp}       Custom Task Runner auf libnx-Primitiven
      filesystem.{h,cpp}        Pfadauflösung, AssetResolver
    renderer/
      software_renderer.{h,cpp} SoftwareSurfacePresentCallback → Framebuffer
    input/
      switch_input.{h,cpp}      Touch → Pointer Events, Pad → Key/Channel
      input_mapping.{h,cpp}     konfigurierbares Button-Mapping (eine Stelle)
    channels/
      platform_channel_registry.{h,cpp}
```

Host-testbar (ohne Switch-Hardware) sollen sein: `PlatformChannelRegistry`,
`TaskScheduler`, `AssetResolver`, `InputMapper`.

## Thread-Zuordnung (Zielbild, noch nicht implementiert)

```text
Main / Platform Thread   libnx-Hauptthread, Applet-Loop, Framebuffer-Present
UI Thread                Dart/Flutter, von der Engine oder von uns gestellt
Render Thread            Raster; im Software-Modus ggf. mit Platform Thread zusammengelegt
IO Thread                Asset-/Bild-Laden
Worker                   engine-intern
```

Ob wir Custom Task Runner stellen oder die Engine ihre Threads selbst erzeugen darf, wird
in Meilenstein 5 anhand des tatsächlichen Verhaltens entschieden – nicht vorab.
