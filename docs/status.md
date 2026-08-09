# Status

Stand: 2026-08-09

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
- [x] **Prozessspeicher gemessen: 3007 MB gesamt, 243 MB belegt** – und zwar im
      *Applet-Modus*, siehe `docs/hardware-target.md`
- [ ] Logging (nxlink/Datei) liefert Ausgabe – nxlink war nicht verbunden, die
      SD-Logdatei wurde noch nicht ausgelesen

## Meilenstein 1b – AOT-Assembly-PoC

- [x] `gen_snapshot` für AArch64 gefunden (Android-arm64-Artefakte des gepinnten SDK,
      kein eigener Dart-Build nötig)
- [x] Dart-Kernel erzeugt (`gen_kernel_aot` + `vm_platform_product.dill`)
- [x] AOT-Snapshot als AArch64-Assembly erzeugt (2,47 MB, 121.924 Zeilen)
- [x] mit devkitA64 assembliert – ohne Anpassung, ohne Warnung
- [x] in eine `.nro` gelinkt (758 KB), alle vier Symbole in der ELF vorhanden
- [x] **Instruktionen liegen als `T` in `.text`** – kein `dlopen`/`mmap`/`mprotect` nötig
- [ ] auf Hardware gestartet, Symboladressen zur Laufzeit geprüft
- [ ] Snapshot *ausgeführt* – braucht die Dart-VM, also Meilenstein 2

## Meilenstein 2 – Engine Cross-Compile PoC

- [ ] Engine-Checkout (`gclient sync`) auf dem Build-Host
- [ ] `current_os = "horizon"` in GN definiert
- [ ] Dart-VM-Portierungsdateien angelegt
- [ ] fml kompiliert
- [ ] Dart VM kompiliert
- [ ] Skia (Software) kompiliert
- [ ] Engine linkt

## Danach

- [ ] Dart AOT lädt
- [ ] Flutter Engine startet
- [ ] erster Frame
- [ ] Touch
- [ ] Controller
- [ ] Platform Channels
