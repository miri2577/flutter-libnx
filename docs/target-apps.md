# Zielanwendungen und Plugin-Kompatibilität

Stand: 2026-08-09.

Ziel ist ein **allgemeiner** Embedder, mit dem bestehende Flutter-Apps möglichst
unverändert laufen – nicht eine auf eine App zugeschnittene Lösung. Als Referenz dient
Referenz-App (`referenz_app`, `Desktop\Referenz-App-Arbeitsstand`, 148 Dart-Dateien), weil es zufällig
der härteste Fall ist: eine Streaming-App mit Videowiedergabe und WebView.

Wichtig für die Erwartungshaltung: An der Reihenfolge der Meilensteine ändert das nichts.
Vor Meilenstein 9 ist keine dieser Fragen relevant – ohne laufende Engine und ersten Frame
gibt es keine Plugins zu portieren.

## Klassifikation nach dem Schema aus dem Auftrag

### A – Pure Dart / pure Flutter, vermutlich kompatibel

Diese brauchen keinen plattformspezifischen Code, nur eine funktionierende Engine und
(teilweise) Netzwerk bzw. Dateisystem:

`cupertino_icons`, `html`, `beautiful_soup_dart`, `xml`, `bloc`, `flutter_bloc`,
`equatable`, `get_it`, `uuid`, `dartz`, `crypto`, `fluent_ui`, `http`, `dio`,
`cookie_jar`, `dio_cookie_manager`, `web_socket_channel`, `hive`, `hive_flutter`

Bedingungen: `dio`/`http`/`web_socket_channel` setzen Sockets und TLS voraus
(Meilenstein 13). `hive` und `cached_network_image` brauchen Dateizugriff
(Meilenstein 14).

### B – Kleine native Plugins mit hoher Hebelwirkung

Diese vier tauchen in nahezu **jeder** Flutter-App auf. Sie sind klein, und ihre
Switch-Implementierung schaltet einen Großteil beliebiger Apps frei. Deshalb gehören sie
in der Plugin-Arbeit nach vorn, nicht die App-spezifischen:

| Plugin | Aufwand | Switch-Entsprechung |
|---|---|---|
| `path_provider` | klein | feste Pfade unter `/switch/flutter_apps/<app-id>/` |
| `shared_preferences` | klein | Datei auf SD, JSON oder Hive-artig |
| `package_info_plus` | klein | Werte aus der NACP bzw. Konstanten |
| `url_launcher` | klein | kein Browser vorhanden – ehrlich als „nicht unterstützt" beantworten statt still zu schlucken |

### C – Native Plugins ohne sinnvolle Switch-Entsprechung

`window_manager`, `screen_retriever` (Desktop-Fensterverwaltung – auf einer Konsole
gegenstandslos), `app_links` (Deep Links), `flutter_secure_storage` (bräuchte ein
Horizon-Pendant für sichere Ablage), `native_dio_adapter` (nutzt die Plattform-HTTP-Stacks
Cronet/NSURLSession – auf der Switch schlicht weglassen und `dio` direkt verwenden).

Für A-bis-C-Apps gilt: Stubs sind erlaubt, aber sie müssen einen klaren Fehler oder
„nicht unterstützt" zurückgeben. Kein stilles `return 0`.

### D – Eigenständige Forschungsprojekte

Zwei Abhängigkeiten von Referenz-App sind keine Portierungsaufgabe, sondern jeweils ein
eigenes Projekt:

**`media_kit` + `media_kit_video`** – setzt libmpv voraus, also faktisch FFmpeg plus
Audio- und GPU-Anbindung. devkitPro hat FFmpeg als Portlib, aber Hardware-Videodecoding
ist im Homebrew-Umfeld nicht ohne Weiteres zugänglich, und Software-Decoding von 1080p auf
den verfügbaren Kernen ist bestenfalls grenzwertig. Genutzt in `main.dart`,
`musik_page.dart`, `radio_page.dart`, `zattoo_service.dart`, `spotify_service.dart`.

**`flutter_inappwebview`** – braucht eine Browser-Engine. Auf der Switch existiert keine,
die Homebrew nutzen könnte. In Referenz-App hängt daran mehr als Bequemlichkeit: Die WebView
löst Captchas und Hoster-Weiterleitungen auf (`captcha_resolver_service.dart`,
`flyfile_resolver_service.dart`, `hoster_resolver_service.dart`,
`webview_resolver_service.dart`, `zattoo_service.dart`). Diese Abläufe müssten anders
gebaut werden – etwa serverseitig auf vorhandener eigener Infrastruktur – statt im Client.

## Was daraus folgt

1. Der Embedder bleibt allgemein. Nichts an Referenz-App rechtfertigt Sonderwege in der Engine.
2. Die Plugin-Arbeit beginnt bei Gruppe B, weil sie App-übergreifend wirkt.
3. Für Referenz-App im Besonderen gilt: UI, Navigation, Scraping, Datenhaltung und Netzwerk
   sind ein realistisches Ziel. Videowiedergabe und WebView sind es vorerst nicht, und das
   sollte man wissen, bevor man Zeit investiert – nicht hinterher.
4. Eine App ohne Video und ohne WebView ist der bessere erste echte Portierungskandidat.
