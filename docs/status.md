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
- [ ] devkitPro/devkitA64 installiert (blockiert: sudo-Passwort in WSL)

## Meilenstein 1 – Minimaler libnx Host

- [ ] `hello_libnx.nro` kompiliert
- [ ] startet auf Hardware
- [ ] Logging (Konsole/nxlink)
- [ ] Controller-Eingabe
- [ ] sauberes Beenden

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
