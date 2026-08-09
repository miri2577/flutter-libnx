# Meilensteine

Jeder Meilenstein hat ein überprüfbares Ergebnis. Abgehakt wird in `docs/status.md`,
und nur nach tatsächlicher Ausführung.

| # | Ziel | Deliverable | Zustand |
|---|---|---|---|
| 0 | Repo + Machbarkeitsanalyse | `docs/feasibility.md`, `docs/portability-matrix.md` | weitgehend erledigt |
| 0b | Toolchain installiert | `aarch64-none-elf-gcc` läuft, `switch-dev` installiert | offen (sudo nötig) |
| 1 | Minimaler libnx-Host | `hello_libnx.nro`: Log, Framebuffer, Pad, sauberes Ende | offen |
| 1b | **AOT-Assembly-PoC** | winziger Dart-AOT-Snapshot als Assembly in eine `.nro` gelinkt, Symbole auflösbar | offen |
| 2 | Engine-Cross-Compile | `current_os = "horizon"` in GN, Engine bis zur Link-Phase | offen |
| 3 | Minimaler Embedder | `FlutterHost` initialisiert die Engine | offen |
| 4 | Software Renderer | erster Flutter-Frame auf dem Bildschirm | offen |
| 5 | Task Runner / Event Loop | dokumentierte, stabile Thread-Zuordnung | offen |
| 6 | Window Metrics | korrekte 1280×720-Metrics inkl. begründeter `pixel_ratio` | offen |
| 7 | Touch Input | Buttons per Touch bedienbar | offen |
| 8 | Controller | Pad-Zustand in Dart sichtbar, konfigurierbares Key-Mapping | offen |
| 9 | Platform Channels | `dev.flutter.libnx/system` antwortet | offen |
| 10 | Plugin-API | `packages/flutter_switch` mit nativer Registrierung | offen |
| 11 | Assets | `flutter_assets` von SD geladen | offen |
| 12 | Fonts | ASCII + Umlaute + Unicode korrekt gerendert | offen |
| 13 | Netzwerk | `HttpClient` gegen HTTPS-Endpunkt | offen |
| 14 | Persistent Storage | Lesen/Schreiben im App-Verzeichnis | offen |
| 15 | GPU Renderer | erst nach stabilem Software-Renderer; vorher Vergleichsdokument | offen |

## Abweichung vom ursprünglichen Plan: Meilenstein 1b

Der Ursprungsplan sieht nach dem libnx-Host direkt den Engine-Cross-Compile vor.
Der Engine-Build ist der mit Abstand teuerste Schritt (Stunden bis Tage, hohes
OOM-Risiko auf 7 GB RAM). Die zentrale Hypothese des Projekts – *Dart-AOT-Code lässt sich
als Assembly in eine NRO linken und ohne `dlopen` referenzieren* – lässt sich aber mit
einem winzigen PoC testen, der die Engine gar nicht braucht: nur `gen_snapshot`,
devkitA64 und ein paar Symbole.

Wenn 1b scheitert, ändert das die gesamte Strategie. Deshalb kommt es vor Meilenstein 2.
