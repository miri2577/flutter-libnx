import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

/// Lokaler HTTP-Proxy für die Nintendo Switch.
///
/// Das statisch gelinkte FFmpeg der Konsole hat kein TLS — mpv meldet für
/// jede https-URL "No protocol handler found" und der Player bleibt stumm.
/// Darts eigener HTTP-Stack kann TLS sehr wohl (ein kompletter WebDAV-Sync
/// läuft darüber). Also zieht dieser Proxy den https-Stream selbst und
/// reicht ihn mpv als `http://127.0.0.1:<port>/<token>` weiter — plain
/// http kann das Konsolen-FFmpeg.
///
/// Range-Anfragen (Spulen!) werden 1:1 durchgereicht, die Antwort-Header
/// (Content-Length, Content-Range, Accept-Ranges) ebenso. Auth-Header
/// (z. B. WebDAV Basic-Auth) wandern in den Proxy und tauchen in der
/// mpv-sichtbaren URL nicht mehr auf.
///
/// HLS: m3u8-Playlisten werden umgeschrieben — jede https-URI (Segmente,
/// Variantenlisten, EXT-X-KEY/MAP) bekommt eine lokale Proxy-Adresse,
/// sonst liefe FFmpeg an der Playlist vorbei direkt in sein fehlendes
/// https. http-URIs bleiben unangetastet.
class LocalStreamProxy {
  LocalStreamProxy._();

  static final LocalStreamProxy instance = LocalStreamProxy._();

  static bool? _isHorizonCached;

  /// Horizon meldet sich als "linux" (Engine-Patch), hat aber kein /proc —
  /// echte Linux-Systeme immer. Billiger und ehrlicher als jede Env-Sonde.
  static bool get isHorizon => _isHorizonCached ??=
      !kIsWeb && Platform.isLinux && !File('/proc/version').existsSync();

  HttpServer? _server;
  final Map<String, _ProxyRoute> _routes = {};
  int _nextToken = 0;

  /// EIN Client für alle Upstream-Anfragen: Keep-Alive nutzt die
  /// TCP+TLS-Verbindung über Range-Requests hinweg wieder. Ein Client je
  /// Anfrage riss den knappen Socket-Pool der Konsole leer — jeder
  /// mpv-Seek kostete einen frischen TLS-Aufbau (ENOBUFS nach dem
  /// ersten Film).
  static final HttpClient _upstreamClient = HttpClient()
    ..autoUncompress = false
    ..idleTimeout = const Duration(seconds: 30)
    ..maxConnectionsPerHost = 4;

  /// Dedupe: dieselbe Ziel-URL bekommt denselben Token. HLS-Playlisten
  /// registrieren jedes Segment — ohne Dedupe wüchse die Tabelle bei jedem
  /// Playlist-Abruf erneut.
  final Map<String, String> _urlToLocal = {};

  Future<void> _ensureServer() async {
    if (_server != null) return;
    _server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    _server!.listen(_handle,
        onError: (Object e) => debugPrint('[Proxy] Serverfehler: $e'));
    debugPrint('[Proxy] lauscht auf 127.0.0.1:${_server!.port}');
  }

  /// Registriert [url] und liefert die lokale Ersatz-URL für mpv.
  Future<String> wrap(String url, Map<String, String> headers) async {
    await _ensureServer();
    final existing = _urlToLocal[url];
    if (existing != null) return existing;
    final token = 't${_nextToken++}';
    _routes[token] = _ProxyRoute(Uri.parse(url), Map.of(headers));
    final local = 'http://127.0.0.1:${_server!.port}/$token';
    _urlToLocal[url] = local;
    return local;
  }

  Future<void> _handle(HttpRequest request) async {
    final token = request.uri.path.replaceFirst('/', '');
    final route = _routes[token];
    if (route == null) {
      request.response.statusCode = HttpStatus.notFound;
      await request.response.close();
      return;
    }

    try {
      final upstream =
          await _upstreamClient.openUrl(request.method, route.target);
      route.headers.forEach((k, v) => upstream.headers.set(k, v));
      // Range/If-Range sind das Spulen — ohne Durchreichen lädt jeder
      // Seek die Datei von vorn.
      for (final h in [HttpHeaders.rangeHeader, HttpHeaders.ifRangeHeader]) {
        final v = request.headers.value(h);
        if (v != null) upstream.headers.set(h, v);
      }
      final response = await upstream.close();

      // HLS-Playlisten muessen umgeschrieben werden: Ihre Segment-URLs sind
      // absolute https-Adressen, die am Proxy vorbei direkt bei FFmpeg
      // landen wuerden — und dessen https gibt es auf der Konsole nicht.
      final contentType =
          response.headers.contentType?.toString().toLowerCase() ?? '';
      final isPlaylist = route.target.path.toLowerCase().contains('.m3u8') ||
          contentType.contains('mpegurl');
      if (isPlaylist && response.statusCode == HttpStatus.ok) {
        final bytes =
            await response.fold<BytesBuilder>(BytesBuilder(), (b, chunk) {
          b.add(chunk);
          return b;
        });
        final body = utf8.decode(bytes.takeBytes(), allowMalformed: true);
        final rewritten = await _rewritePlaylist(body, route);
        final payload = utf8.encode(rewritten);
        request.response.statusCode = HttpStatus.ok;
        request.response.headers.contentType =
            ContentType.parse('application/vnd.apple.mpegurl');
        request.response.contentLength = payload.length;
        request.response.add(payload);
        await request.response.close();
        return;
      }

      request.response.statusCode = response.statusCode;
      response.headers.forEach((name, values) {
        final n = name.toLowerCase();
        // Hop-by-Hop-Header gehören der jeweiligen Verbindung.
        if (n == 'transfer-encoding' || n == 'connection') return;
        for (final v in values) {
          request.response.headers.set(name, v);
        }
      });
      if (response.contentLength >= 0) {
        request.response.contentLength = response.contentLength;
      }
      await response.pipe(request.response);
    } catch (e) {
      debugPrint('[Proxy] $token → ${route.target.host}: $e');
      try {
        request.response.statusCode = HttpStatus.badGateway;
        await request.response.close();
      } catch (_) {}
    }
  }

  /// Schreibt jede URI einer m3u8-Playlist auf den Proxy um: Segmentzeilen
  /// (alles ohne '#') und URI="..."-Attribute (EXT-X-KEY, EXT-X-MAP,
  /// EXT-X-MEDIA). Relative Adressen werden gegen die Playlist-URL
  /// aufgelöst; http-Adressen bleiben unangetastet (das kann FFmpeg selbst,
  /// und mpvs http-header-fields gelten dort weiter).
  Future<String> _rewritePlaylist(String body, _ProxyRoute route) async {
    Future<String> localFor(String raw) async {
      final absolute = route.target.resolve(raw.trim()).toString();
      if (!absolute.startsWith('https://')) return raw.trim();
      return wrap(absolute, route.headers);
    }

    final uriAttribute = RegExp('URI="([^"]+)"');
    final out = <String>[];
    for (final line in body.split('\n')) {
      final trimmed = line.trimRight();
      if (trimmed.trim().isEmpty) {
        out.add(trimmed);
      } else if (trimmed.trimLeft().startsWith('#')) {
        var rewritten = trimmed;
        for (final match in uriAttribute.allMatches(trimmed).toList()) {
          final replacement = await localFor(match.group(1)!);
          rewritten =
              rewritten.replaceFirst(match.group(0)!, 'URI="$replacement"');
        }
        out.add(rewritten);
      } else {
        out.add(await localFor(trimmed));
      }
    }
    return out.join('\n');
  }
}

class _ProxyRoute {
  final Uri target;
  final Map<String, String> headers;
  _ProxyRoute(this.target, this.headers);
}
