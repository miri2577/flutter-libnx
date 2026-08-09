# Porting Notes

Fortlaufendes Protokoll technischer Befunde. Neueste Einträge oben.

Format je Eintrag: Befund · Beleg (Datei:Zeile oder Kommando) · Konsequenz.

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
