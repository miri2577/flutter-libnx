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
| `url_launcher` | klein | über das System-Web-Applet (`webPageCreate`/`webConfigShow`) – siehe Korrektur unten |

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
Es gibt womöglich einen deutlich billigeren Weg über das System-Applet – siehe unten.

**`flutter_inappwebview`** – siehe eigenen Abschnitt unten. Die frühere Aussage „auf der
Switch existiert keine Browser-Engine" war falsch.

## Korrektur 2026-08-09: Es gibt eine Browser-Engine, aber nicht als View

Die Switch hat einen systemeigenen Browser (NetFront NX, WebKit-basiert) als
**Applet**, und libnx spricht ihn vollständig an: `applets/web.h`, 862 Zeilen.
Homebrew kann ihn starten (`webPageCreate` + `webConfigShow`), und die Domain der
übergebenen URL wird automatisch auf die Whitelist gesetzt (`web.h:315`) – eine
Host-Application ist dafür also nicht nötig.

Was damit **nicht** geht:

- **Kein Einbetten.** Es ist ein eigenes Vollbild-Applet, kein View. `InAppWebView` als
  Widget im Flutter-Baum ist damit nicht abbildbar, ebenso wenig eine Headless-WebView.
- **Kein Cookie-Zugriff.** `web.h` enthält keine einzige Cookie-Funktion (geprüft per
  Suche über die gesamte Datei). Zurück kommen nur Exit-Grund, letzte URL und ein paar
  Share-/Media-Flags (`webReplyGet*`, `web.h:743-792`).

Was damit **geht**:

- Ein Switch-eigenes Plugin `switch_web`, das eine URL im Systembrowser öffnet und die
  letzte URL zurückgibt (`webConfigSetCallbackUrl` + `webReplyGetLastUrl`). Das deckt
  OAuth-artige Rückruf-Abläufe ab.
- `url_launcher` ist damit **doch** implementierbar – die frühere Einordnung „kein Browser
  vorhanden" in Gruppe B stimmt so nicht.

Für Referenz-App bleibt die Captcha-Auflösung trotzdem schwierig: Ein
Cloudflare-Clearance-Cookie ist HttpOnly und wäre selbst mit JavaScript auf einer eigenen
Zwischenseite nicht auslesbar, um es über die Callback-URL zurückzureichen. Ob der bei
FlyFile benötigte Wert ein Cookie oder ein per API zurückgegebener Token ist, müsste man
sich im konkreten Ablauf ansehen – im zweiten Fall wäre der Callback-Weg gangbar.

## Möglicherweise wichtiger Nebenbefund: Video über das Web-Applet

Dasselbe Applet kann direkt als **Medienspieler** starten:
`WebArgType_BootAsMediaPlayer` [2.0.0+], dazu `MediaAutoPlay`, `MediaPlayerUi`,
`MediaPlayerSpeedControl`, `MediaPlayerAutoClose`, und als Rückgabe
`webReplyGetMediaPlayerAutoClosedByCompletion` – „Wiedergabe bis zum Ende gelaufen".
Genau darüber läuft auch `webYouTubeVideoCreate`.

Falls dieser Spieler eine HLS-URL akzeptiert, bräuchte Videowiedergabe **kein** libmpv und
kein FFmpeg: Der Embedder würde die Stream-URL an das Systemapplet übergeben. Preis wäre,
dass die Wiedergabe das Bild komplett übernimmt – keine Flutter-Overlays, keine Steuerung
aus Dart heraus während des Abspielens, Rückkehr erst beim Beenden.

**Ungeprüft und vor jeder Planung zu klären:** ob HLS unterstützt wird, ob das Applet aus
Homebrew heraus im Applet-Modus (hbmenu über Album) überhaupt startet, und welche
Speichergrenzen dort gelten. Das sind Hardwarefragen, keine Header-Fragen – sie gehören in
ein kleines Testprogramm, sobald die Toolchain steht.

## Der Bauweg muss ebenso allgemein sein wie der Embedder (2026-08-11)

Bis hierher wurde das Beispiel von Hand zusammengesetzt: `pubspec.yaml` eigens
angelegt, `AssetManifest.json` und `FontManifest.json` selbst geschrieben, die
Icon-Schrift von Hand kopiert. Das trägt für **ein** Beispiel und bricht bei der
ersten Anwendung, die eigene Schriften, Bilder oder Assets aus Paketen
mitbringt.

Wie es auffiel: Auf dem Bildschirm stand an jeder Icon-Stelle das
Ersatzkästchen mit Kreuz. `Icons.touch_app` ist keine Grafik, sondern ein
Zeichen der Schriftart „MaterialIcons" – ein Asset, das ein gewöhnliches
`flutter build` mitliefert und unser handgeschriebenes Manifest nicht kannte.
Genau diese Klasse Fehler wiederholt sich bei jedem Projekt neu, solange die
Assets nicht aus dem Projekt selbst kommen.

**Zielbild:** Ein Werkzeug nimmt ein unverändertes Flutter-Projekt und liefert
eine `.nro`:

| Schritt | Werkzeug | Ergebnis |
|---|---|---|
| Assets | `flutter build bundle` im Projekt | `flutter_assets/` mit beiden Manifesten, Schriften und Projekt-Assets |
| Kernel | `gen_kernel_aot` mit `--packages` aus dem Projekt | `app.dill` |
| Snapshot | eigenes `clang_x64/gen_snapshot_product` | `app_aot.s` |
| NRO | devkitA64, Embedder, RomFS | `.nro` |

Nur der dritte Schritt ist projektspezifisch gebaut; die anderen drei sind
Standardwerkzeuge oder mechanisch.

**Erster Befund dazu:** `flutter build bundle` verlangt die übliche
Projektstruktur mit `lib/main.dart`. Unser Beispiel legt `main.dart` daneben und
scheitert daran – ein Hinweis darauf, dass die Beispiele dem folgen sollten, was
echte Projekte mitbringen, statt eigene Wege zu gehen. Sonst prüft der Bauweg
etwas, das später niemand so verwendet.

## Was daraus folgt

1. Der Embedder bleibt allgemein. Nichts an Referenz-App rechtfertigt Sonderwege in der Engine.
2. Die Plugin-Arbeit beginnt bei Gruppe B, weil sie App-übergreifend wirkt.
3. Für Referenz-App im Besonderen gilt: UI, Navigation, Scraping, Datenhaltung und Netzwerk
   sind ein realistisches Ziel. Videowiedergabe und WebView sind es vorerst nicht, und das
   sollte man wissen, bevor man Zeit investiert – nicht hinterher.
4. Eine App ohne Video und ohne WebView ist der bessere erste echte Portierungskandidat.
