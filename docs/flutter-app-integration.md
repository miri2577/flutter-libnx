# Flutter-Apps auf der Switch: Integrationsleitfaden

Wie man eine **echte** Flutter-App (Netzwerk, Bilder, Video) auf Horizon
lauffähig macht — die app-seitigen Muster, die sich an einer grossen
Referenz-App (Netzwerk-, Bild- und Video-lastige UI) herausgeschält haben.
Der Embedder (dieses Repo) stellt
Engine, GL, Dateisystem und Kanäle; dieser Leitfaden sammelt, was auf der
**Dart-Seite** jeder App nötig ist. Kopiervorlagen liegen in `dart_helpers/`.

Alle Befunde sind auf echter Hardware bestätigt (Switch, Atmosphère,
Anwendungsmodus). Reihenfolge etwa nach Aufwand/Wichtigkeit.

---

## 1. Start-Guards: die App darf nicht vor `runApp()` sterben

Horizon meldet sich als `Platform.operatingSystem == "linux"` (Engine-Patch,
sonst wirft `defaultTargetPlatform`). Damit laufen App-Teile, die den
Linux-Desktop-Zweig annehmen — und werfen, weil es die Dienste nicht gibt.
Jeden solchen Aufruf in `main()` **vor `runApp()`** kapseln, sonst bleibt der
Bildschirm schwarz:

```dart
try { MediaKit.ensureInitialized(); } catch (_) {}
if (Platform.isLinux) {                 // Horizon fällt hier hinein
  try { await windowManager.ensureInitialized(); ... } catch (_) {}
}
```

Eine ungefangene `MissingPluginException` (z. B. `window_manager`) in `main()`
tötet die App vor dem ersten Frame.

**Horizon erkennen** (ohne eigenen Kanal): meldet sich als „linux", hat aber
kein `/proc`:

```dart
static bool get isHorizon =>
    !kIsWeb && Platform.isLinux && !File('/proc/version').existsSync();
```

## 2. Netzwerk: kein curl, kein WebView, aber Darts TLS wird akzeptiert

Drei harte Fakten auf Horizon:

- **Keine Kindprozesse** → jeder `Process.run('curl', …)`-Fallback wirft nur
  `ProcessException` und verdeckt den echten Fehler. curl-Pfade komplett
  abschalten (`_hasCurlFallback == false`).
- **Kein InAppWebView** → WebView-basierte Resolver/Flows scheitern.
- **Der System-DNS kann lügen** (netzseitige Sperren), aber **Google-DoH ist
  erreichbar** und **Darts BoringSSL-TLS-Fingerprint wird akzeptiert** —
  auch von Cloudflare-Seiten (auf Hardware/PC mit HTTP 200 geprüft). Es
  braucht also **keinen** Server-Relay, nur den richtigen Verbindungsweg.

**Die Falle:** `HttpClient.connectionFactory` kann das NICHT leisten. Gibt man
einen einfachen `Socket` zurück, macht die Engine **kein** TLS darüber
(→ „400 The plain HTTP request was sent to HTTPS port"). Und `ConnectionTask`
ist `final` — man kann keinen eigenen Wrapper um einen `SecureSocket` bauen.

**Die Lösung** (`dart_helpers/horizon_http.dart`): ein schlanker Roh-GET,
der selbst zur DoH-IP verbindet und TLS mit **Hostname-SNI** aufsetzt:

```dart
final ip = await doh(uri.host);                    // dns.google
var socket = await Socket.connect(ip, 443);
socket = await SecureSocket.secure(socket, host: uri.host,   // ← SNI!
                                   onBadCertificate: (_) => true);
socket.add(utf8.encode('GET $path HTTP/1.1\r\nHost: ${uri.host}\r\n...'));
```

Details, die der Helfer schon erledigt: Redirects, gzip, **chunked** —
und das Lesen **an der chunked-Endmarke / Content-Length beenden**, weil
manche Server (Cloudflare) trotz `Connection: close` kein FIN senden und ein
reines Bis-Schluss-Lesen sonst jedes Mal 20–25 s ins Timeout läuft, obwohl
die Daten längst vollständig sind.

## 3. Socket-Limit: max. ~4 gleichzeitige Verbindungen

Die Konsole hat nur **16 BSD-Sessions** (siehe `SocketInitConfig`, im
Embedder auf 16 gesetzt). Öffnen viele Widgets gleichzeitig eigene Sockets
(z. B. Dutzende Cover auf einer Seite), erschöpft das den Pool und der
Prozess stirbt **hart am Kernel** (2011-0102, „out of sessions") — **ohne**
Crash-Handler-Ausgabe. `horizon_http.dart` hat deshalb eine
Parallelitäts-Bremse (Semaphore, Standard 4). Lässt man App-eigene Clients
(Dio o. Ä.) daneben laufen, Luft im Pool lassen.

## 4. Bilder: Flutters Bild-Laden umgeht euer Netzwerk

`Image.network` / `CachedNetworkImage` nutzen einen **eigenen** `HttpClient`
mit System-DNS — sie kommen nicht an gesperrte/Custom-Hosts. Für Bilder
braucht es ein eigenes Widget, das die Bytes über `HorizonHttp.getBytes`
holt und per `Image.memory` zeigt (In-Memory-Cache fürs Scrollen). Muster
im `PlatformImage` der Referenz-App; Kernpunkte:

- **`cacheWidth` setzen!** Volle Poster-Auflösung erzeugt große GPU-Texturen;
  ~120 davon erschöpfen den Tegra über nouveau → **harter Fault ohne
  Handler**. Ein Cover im Raster braucht ~360 px, nicht 1000.
- `PaintingBinding.instance.imageCache.maximumSizeBytes` niedrig halten
  (z. B. 40 MB) — begrenzt die dekodierten Live-Bilder.

## 5. Video (media_kit): den Player NIE mitten in der Sitzung zerstören

**Der wichtigste Video-Befund.** `player.dispose()` ruft
`mpv_terminate_destroy`; dessen Thread-Abbau hinterlässt auf Horizon
geliehene Stacks (`svcMapMemory`-Regionen, die libnx nicht abräumt) und
**korrumpiert den Heap** — die laufende Sitzung läuft dann bei der nächsten
Allokation hinein und stürzt ab (SD-Log endet im Teardown, teils mit
Binärmüll). Ein leichter Player (Intro) übersteht seinen **einen** Abbau;
ein schwerer (große Caches, `hwdec`) nicht.

**Lösung:** genau **eine** persistente `Player`+`VideoController`-Instanz für
die App-Laufzeit. Beim Verlassen einer Player-Seite nur `player.stop()`, nie
`dispose()`. Damit läuft `mpv_terminate_destroy` gar nicht erst.

```dart
static Player? _shared;
player = _shared ??= Player(...);   // einmal erzeugen, ewig behalten
...
@override void dispose() { player.stop(); /* KEIN player.dispose() */ }
```

Weiteres zu media_kit auf Horizon:
- Es braucht `/tmp` (mkdir). Es **parst** die `NativeReferenceHolder`-Datei
  darin als Zeiger — Altlasten beim Start löschen (Dateiname **ohne**
  führenden Punkt: `com.alexmercerind.media_kit.NativeReferenceHolder.<pid>`;
  die PID des Wirtsprozesses ist über hbmenu-Reloads konstant). Grundursache:
  der Code läuft nur, weil `kDebugMode` im AOT-Build wahr ist (gen_kernel
  ohne `-Ddart.vm.product`).
- HTTPS-Streams: das statisch gelinkte **FFmpeg hat kein TLS**
  („No protocol handler found"). Stream über einen lokalen Proxy laufen
  lassen (`dart_helpers/local_stream_proxy.dart`): Dart zieht das https,
  reicht es mpv als `http://127.0.0.1:<port>/<token>`. Range-Requests
  durchreichen (Spulen), HLS-Playlisten umschreiben (Segment-URLs auf den
  Proxy zeigen lassen). **Ein** Upstream-Client mit Keep-Alive, sonst frisst
  jeder Seek einen neuen TLS-Aufbau aus dem knappen Socket-Pool.

## 6. Was der Embedder inzwischen GENERISCH erledigt (nichts zu tun)

Diese Punkte brauchten frueher App-Arbeit und sind jetzt fuer jede App
geloest — bestaetigt mit zwei unveraenderten Fremdprojekten
(Piyushhhhh/flutter-games, flutter/gallery) auf Hardware:

- **Virtueller Cursor** (main.cpp + egl_horizon.cpp): Rechter Stick bewegt
  einen gezeichneten Zeiger, A/ZR klickt als Touch-Ereignis, Taste halten +
  Stick = Drag/Swipe. Damit sind reine GestureDetector-Apps ohne
  fokussierbare Widgets voll bedienbar. Solange der Zeiger aktiv ist,
  sendet A kein Enter (keine Doppel-Aktivierung); nach 5 s Leerlauf blendet
  er aus, dann gilt wieder das Fokus-Modell (linker Stick/DPAD/A/B).
- **System-Locale** (main.cpp): `setGetSystemLanguage` →
  `FlutterEngineUpdateLocales`. Ohne das bekam das Framework eine LEERE
  Locale-Liste — Apps, die im `localeListResolutionCallback`
  `locales.first` lesen (Flutter Gallery), starben mit StateError im
  ersten Build: weisser Bildschirm.
- **`-Product`-Schalter** (build-dart-app.ps1): setzt
  `-Ddart.vm.product=true` beim Kernel — kDebugMode wird falsch,
  Debug-Pfade fallen aus dem AOT. Standard fuer fremde Apps. NICHT fuer
  Apps mit media_kit auf Horizon (dessen NativeReferenceHolder-Pfad lebt
  vom wahren kDebugMode!).

Typische App-seitige Restarbeit bei ARCHIVIERTEN Projekten ist reine
Flutter-Versions-Migration (waere auf jedem Desktop genauso faellig):
CardTheme→CardThemeData, BottomAppBarTheme→BottomAppBarThemeData,
`package:flutter_gen`-l10n auf `output-dir` umstellen, veraltete
transitive Pakete per `dependency_overrides` anheben (win32,
google_fonts).

## 7. Zusammengefasst: die Kopiervorlagen

| Datei | Zweck |
|---|---|
| `dart_helpers/horizon_http.dart` | GET über DoH-IP + TLS-mit-SNI; Redirects/gzip/chunked; Bytes für Bilder; Parallelitäts-Bremse |
| `dart_helpers/local_stream_proxy.dart` | Lokaler http-Proxy für HTTPS-Video an mpv; Range + HLS-Umschreibung |

Beide hängen nur an `dart:*` und `package:flutter/foundation.dart` — direkt
in ein beliebiges Projekt kopierbar. Der Rest (welche Domains, welche
Resolver, welches UI) ist app-spezifisch und gehört ins jeweilige App-Repo.
