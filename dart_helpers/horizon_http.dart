import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';

/// Eigener HTTP-GET für die Nintendo Switch (Horizon).
///
/// Warum nötig: Horizon hat keine Kindprozesse (kein curl-Ausweg des
/// Desktops), und der System-DNS lügt bei gesperrten Domains (Provider).
/// Darts BoringSSL-TLS wird von Cloudflare-geschuetzten Seiten und den Hostern aber
/// AKZEPTIERT (auf Hardware/PC geprüft, HTTP 200) — es fehlte nur ein Weg,
/// zur DoH-IP zu verbinden UND die richtige SNI (Hostname) zu setzen.
/// Darts `HttpClient.connectionFactory` kann das nicht (einfacher Socket →
/// kein TLS, `ConnectionTask` ist `final`). Deshalb hier ein schlanker
/// Roh-GET über `SecureSocket.secure(host: …)`: verbindet zur IP, TLS mit
/// Hostname-SNI, folgt Redirects, entpackt gzip/chunked, beendet das Lesen
/// an der chunked-Endmarke bzw. Content-Length (video-seite sendet kein FIN).
///
/// Grenzen: nur GET, kein Keep-Alive, keine Cookies. Der Video-Stream selbst
/// läuft über den LocalStreamProxy.
class HorizonHttp {
  HorizonHttp._();

  static const String defaultUserAgent =
      'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36';

  static final Map<String, String> _dohCache = {};

  // Parallelitaets-Bremse: Jede Anfrage oeffnet einen eigenen TLS-Socket.
  // Die Konsole hat nur 16 BSD-Sessions — laufen beim Zurueckblaettern
  // Dutzende Cover gleichzeitig (plus WebDAV-Cloud-Scan), erschoepft das den
  // Session-Pool und der Prozess stirbt hart am Kernel (2011-0102, "out of
  // sessions"), ohne dass der Crash-Handler greift. Max. 6 gleichzeitig,
  // der Rest wartet in der Schlange.
  static const int _maxConcurrent = 4;
  static int _active = 0;
  static final List<Completer<void>> _queue = [];

  static Future<void> _acquire() async {
    if (_active < _maxConcurrent) {
      _active++;
      return;
    }
    final c = Completer<void>();
    _queue.add(c);
    await c.future;
  }

  static void _release() {
    if (_queue.isNotEmpty) {
      _queue.removeAt(0).complete();
    } else {
      _active--;
    }
  }

  /// DoH über dns.google (System-DNS reicht dorthin — Google-DNS wird nicht
  /// gesperrt). Rückgabe null, wenn keine A-Antwort kommt.
  static Future<String?> resolve(String host) async {
    if (_dohCache.containsKey(host)) return _dohCache[host];
    final client = HttpClient()..connectionTimeout = const Duration(seconds: 8);
    try {
      final req = await client
          .getUrl(Uri.parse('https://dns.google/resolve?name=$host&type=A'));
      final res = await req.close();
      final json =
          jsonDecode(await res.transform(utf8.decoder).join()) as Map<String, dynamic>;
      for (final answer in (json['Answer'] as List? ?? const [])) {
        if (answer['type'] == 1) {
          final ip = answer['data'] as String;
          _dohCache[host] = ip;
          return ip;
        }
      }
    } catch (e) {
      debugPrint('[HorizonHttp] DoH $host: $e');
    } finally {
      client.close();
    }
    return null;
  }

  /// Roh-Bytes (für Bilder u. Ä.).
  static Future<HorizonBytes> getBytes(String url,
      {Map<String, String>? headers, int maxRedirects = 8}) {
    return _fetch(url, headers: headers, maxRedirects: maxRedirects, quiet: true);
  }

  /// Textantwort (HTML/JSON).
  static Future<HorizonResponse> get(String url,
      {Map<String, String>? headers, int maxRedirects = 8}) async {
    final raw =
        await _fetch(url, headers: headers, maxRedirects: maxRedirects);
    return HorizonResponse(raw.status, raw.headers,
        utf8.decode(raw.body, allowMalformed: true), raw.finalUrl);
  }

  static Future<HorizonBytes> _fetch(
    String url, {
    Map<String, String>? headers,
    int maxRedirects = 8,
    bool quiet = false,
  }) async {
    await _acquire();
    try {
      return await _fetchInner(url,
          headers: headers, maxRedirects: maxRedirects, quiet: quiet);
    } finally {
      _release();
    }
  }

  static Future<HorizonBytes> _fetchInner(
    String url, {
    Map<String, String>? headers,
    int maxRedirects = 8,
    bool quiet = false,
  }) async {
    var uri = Uri.parse(url);
    for (var hop = 0; hop <= maxRedirects; hop++) {
      final https = uri.scheme == 'https';
      final port = uri.hasPort ? uri.port : (https ? 443 : 80);
      final ip = await resolve(uri.host) ?? uri.host;
      if (!quiet) debugPrint('[HorizonHttp] hop $hop ${uri.host} -> $ip:$port');

      Socket socket =
          await Socket.connect(ip, port, timeout: const Duration(seconds: 12));
      if (https) {
        socket = await SecureSocket.secure(socket,
            host: uri.host, onBadCertificate: (_) => true);
      }

      final path = uri.hasQuery
          ? '${uri.path}?${uri.query}'
          : (uri.path.isEmpty ? '/' : uri.path);
      final head = StringBuffer()
        ..write('GET $path HTTP/1.1\r\n')
        ..write('Host: ${uri.host}\r\n')
        ..write('User-Agent: ${headers?['User-Agent'] ?? defaultUserAgent}\r\n')
        ..write('Accept: ${headers?['Accept'] ?? '*/*'}\r\n')
        ..write('Accept-Language: de-DE,de;q=0.9,en;q=0.8\r\n')
        ..write('Accept-Encoding: gzip\r\n');
      headers?.forEach((k, v) {
        final lk = k.toLowerCase();
        if (lk != 'user-agent' && lk != 'accept' && lk != 'host') {
          head.write('$k: $v\r\n');
        }
      });
      head.write('Connection: close\r\n\r\n');
      socket.add(utf8.encode(head.toString()));
      await socket.flush();

      final buf = <int>[];
      int sep = -1;
      int status = 0;
      final resHeaders = <String, String>{};
      var chunked = false;
      int? contentLength;
      try {
        await for (final chunk in socket.timeout(const Duration(seconds: 20))) {
          buf.addAll(chunk);
          if (sep < 0) {
            for (var i = 0; i + 3 < buf.length; i++) {
              if (buf[i] == 13 && buf[i + 1] == 10 &&
                  buf[i + 2] == 13 && buf[i + 3] == 10) {
                sep = i;
                break;
              }
            }
            if (sep >= 0) {
              final lines = latin1.decode(buf.sublist(0, sep)).split('\r\n');
              status = int.parse(lines.first.split(' ')[1]);
              for (final line in lines.skip(1)) {
                final c = line.indexOf(':');
                if (c > 0) {
                  resHeaders[line.substring(0, c).trim().toLowerCase()] =
                      line.substring(c + 1).trim();
                }
              }
              chunked = (resHeaders['transfer-encoding'] ?? '')
                  .toLowerCase().contains('chunked');
              contentLength = int.tryParse(resHeaders['content-length'] ?? '');
            }
          }
          if (sep >= 0) {
            final bodyLen = buf.length - (sep + 4);
            if (contentLength != null && bodyLen >= contentLength) break;
            if (chunked && buf.length >= 7) {
              final n = buf.length;
              if (buf[n-7]==13 && buf[n-6]==10 && buf[n-5]==48 &&
                  buf[n-4]==13 && buf[n-3]==10 && buf[n-2]==13 && buf[n-1]==10) {
                break;
              }
            }
          }
        }
      } on TimeoutException {
        if (!quiet) {
          debugPrint('[HorizonHttp] Lese-Timeout, ${buf.length} B');
        }
      }
      socket.destroy();

      if (sep < 0) throw const HttpException('HorizonHttp: kein Header-Ende');
      final bytes = Uint8List.fromList(buf);
      var body = Uint8List.sublistView(bytes, sep + 4);

      if (chunked) body = _dechunk(body);
      if ((resHeaders['content-encoding'] ?? '').toLowerCase().contains('gzip')) {
        try {
          body = Uint8List.fromList(gzip.decode(body));
        } catch (_) {/* unvollständig/kein gzip — Rohdaten behalten */}
      }

      if (!quiet) {
        debugPrint('[HorizonHttp] ${uri.host} -> HTTP $status, Body ${body.length} B');
      }

      if (status >= 300 && status < 400 && resHeaders['location'] != null) {
        uri = uri.resolve(resHeaders['location']!);
        continue;
      }
      return HorizonBytes(status, resHeaders, body, uri.toString());
    }
    throw const HttpException('HorizonHttp: zu viele Redirects');
  }

  static Uint8List _dechunk(Uint8List data) {
    final out = BytesBuilder();
    var i = 0;
    while (i < data.length) {
      var j = i;
      while (j + 1 < data.length && !(data[j] == 13 && data[j + 1] == 10)) {
        j++;
      }
      final sizeLine = latin1.decode(data.sublist(i, j)).split(';').first.trim();
      if (sizeLine.isEmpty) break;
      final size = int.tryParse(sizeLine, radix: 16) ?? 0;
      if (size == 0) break;
      final start = j + 2;
      if (start + size > data.length) break;
      out.add(data.sublist(start, start + size));
      i = start + size + 2;
    }
    return out.takeBytes();
  }
}

class HorizonBytes {
  final int status;
  final Map<String, String> headers;
  final Uint8List body;
  final String finalUrl;
  HorizonBytes(this.status, this.headers, this.body, this.finalUrl);
}

class HorizonResponse {
  final int status;
  final Map<String, String> headers;
  final String body;
  final String finalUrl;
  HorizonResponse(this.status, this.headers, this.body, this.finalUrl);
}
