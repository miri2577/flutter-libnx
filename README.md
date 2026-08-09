# flutter-libnx

Experimenteller Flutter-Engine-Embedder für Nintendo Switch Homebrew (libnx / devkitA64).

Ziel: eine echte Flutter-App als native `.nro` unter Horizon OS ausführen – kein Android,
kein SDL-Ersatz-UI, keine Screenshots. Die UI muss tatsächlich von der Flutter Engine
gerendert werden.

Das Projekt ist Forschungsarbeit und **läuft noch nicht**.

## Was funktioniert?

Nichts auf Hardware. Bisher liegt die Machbarkeitsanalyse vor
(`docs/feasibility.md`) – gegen echten Quellcode der gepinnten Version geprüft, nicht
gegen Erinnerung oder Blogposts.

## Was funktioniert noch nicht?

Alles Übrige. Aktueller Detailstand: `docs/status.md`.

## Versionen

| Komponente | Pin |
|---|---|
| Flutter (Monorepo) | `db50e20168db8fee486b9abf32fc912de3bc5b6a` – Tag `3.41.6`, stable |
| Dart SDK | `02abc57898bebc334a997e609ce5827c8ef207d7` (Dart 3.11.4) |
| Skia | `a183ded9ad67d998a5b0fe4cd86d3ef5402ffb45` |
| libnx (Referenz) | `dbcc1beafc6b47b5ffbeb8ba82463a7d45da40bb` |
| devkitA64 | noch nicht installiert |
| Build-Host | WSL2 Ubuntu 24.04 (x86_64) |

Warum 3.41.6 und nicht `main`: Ab 3.44 ist Skia auf Android entfernt worden, die Zukunft
des Software-Renderers für Embedder-API-Nutzer ist damit offen. 3.41.6 ist die
Bootstrap-Version; das Upgrade-Risiko ist in `docs/feasibility.md` §2 beschrieben.

## Kernentscheidungen bisher

1. **Software Renderer zuerst**, keine GPU. `FlutterSoftwareRendererConfig` existiert in
   der gepinnten Version.
2. **Dart AOT, kein JIT.** Der Snapshot wird als AArch64-Assembly erzeugt und in die `.nro`
   gelinkt, statt zur Laufzeit per `dlopen` geladen zu werden. Damit entfallen `dlopen`
   und ausführbarer Laufzeitspeicher. Belege: `docs/feasibility.md` §1.
3. **Neues GN-Target `current_os = "horizon"`**, nach dem Vorbild des QNX-Ports –
   ausdrücklich *nicht* `#define __linux__`.

## Wie baue ich das Projekt?

Voraussetzung ist die Toolchain (einmalig, braucht ein interaktives Terminal wegen sudo):

```bash
# in WSL (Ubuntu 24.04)
cd /mnt/e/flutter-libnx && ./scripts/setup-devkitpro.sh
```

Danach das Beispiel aus Meilenstein 1:

```bash
export DEVKITPRO=/opt/devkitpro
cd /mnt/e/flutter-libnx/examples/hello_libnx && make
```

Ergebnis wäre `examples/hello_libnx/hello_libnx.nro`. **Das ist bisher nie gelaufen** –
der Code ist geschrieben und alle libnx-Aufrufe sind gegen die echten Header geprüft,
aber ohne Toolchain gibt es keinen Compilerdurchlauf. Siehe `docs/status.md`.

Referenzquellen (libnx, `embedder.h` der gepinnten Version) neu holen:

```powershell
.\scripts\fetch-reference.ps1
```

## Nächster Meilenstein

Meilenstein 1: `hello_libnx.nro` – Framebuffer, Logging, Pad-Eingabe, sauberes Beenden.
Parallel Meilenstein 1b: AOT-Assembly-PoC, der die zentrale Hypothese des Projekts prüft,
bevor der teure Engine-Build angefasst wird. Siehe `docs/milestones.md`.

## Verzeichnisse

```text
docs/         Analyse, Architektur, Status, Portierungsprotokoll
embedder/     C++-Embedder (noch leer)
examples/     Beispiel-Apps (noch leer)
scripts/      Setup- und Build-Skripte
patches/      Patches gegen die Flutter Engine, je mit Begründung
third_party/  Referenz-Checkouts, nicht versioniert
```
