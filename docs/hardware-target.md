# Testgerät und Laufzeitumgebung

Stand: 2026-08-09.

## Gerät

| Was | Wert | Quelle |
|---|---|---|
| Konsole | Nintendo Switch 1, dauerhaft gemoddet (Modchip) | Angabe des Nutzers |
| Bootloader | Hekate 6.0.7 | Angabe des Nutzers |
| CFW | Atmosphère, läuft | Angabe des Nutzers |
| Horizon-Firmware | **unbekannt – zu erfragen** | – |
| Atmosphère-Version | **unbekannt – zu erfragen** | – |

Der Modchip ist für dieses Projekt mehr als Bequemlichkeit: Er erlaubt beliebig häufiges
Neustarten ohne RCM-Jig, und das ist bei einer Portierung, die reihenweise abstürzen wird,
der Unterschied zwischen zügigem Arbeiten und Quälerei.

## Die wichtigste Randbedingung: Applet-Modus vs. Anwendungsmodus

Homebrew läuft auf der Switch in zwei sehr unterschiedlichen Umgebungen:

Die verbreitete Angabe lautet: Applet-Modus (Start über das Album) ~440 MB,
Anwendungsmodus (Titelübernahme, R beim Starten eines Spiels halten) 3 GB+.

**Auf diesem Gerät gemessen (2026-08-09), und das Ergebnis widerspricht dem:**

```text
Laufzeitumgebung: LibraryApplet (Applet-Modus)
Speicher gesamt : 3007 MB
davon belegt    : 243 MB
```

Gemessen mit `svcGetInfo(InfoType_TotalMemorySize / InfoType_UsedMemorySize)` aus
`hello_libnx`, gestartet über das Album. Der Applet-Modus hat hier also praktisch den
vollen Anwendungsspeicher.

Wahrscheinlichste Erklärung: Atmosphère erlaubt über `system_settings.ini`, die
Heap-Größe von hbloader im Applet-Modus zu konfigurieren, und das verwendete CFW-Paket
schöpft das aus. Die 440 MB sind eine ältere bzw. unkonfigurierte Standardannahme.

**Entscheidung für das MVP:** Der Applet-Modus wird **nicht** ausgeschlossen. Auf diesem
Gerät reicht der Speicher, und der Testzyklus über hbmenu und nxlink ist damit erheblich
bequemer als eine Titelübernahme pro Durchlauf.

**Aber:** Diese Zahl gilt für *diese* Konfiguration, nicht allgemein. Auf einem Gerät mit
unveränderter Atmosphère-Konfiguration können es 440 MB sein. Sobald die Engine läuft,
gehört der Speicherbedarf gemessen und mit beiden Zahlen verglichen – und die
Titelübernahme bleibt der Rückfallweg, falls es eng wird.

## Zugriff vom Entwicklungsrechner

Aktueller Stand: **kein Zugriff**.

- USB: Es meldet sich ein Nintendo-HID-Gerät (`VID_057E&PID_2000`), aber mit Fehlerstatus.
  Atmosphère stellt von sich aus keine brauchbare USB-Schnittstelle bereit.
- SD-Karte: steckt in der Konsole, nicht im PC. Kein Wechseldatenträger sichtbar.
- Netzwerk: Kein Gerät mit Nintendo-MAC in der ARP-Tabelle.

Der vorgesehene Weg ist **nxlink über WLAN**, sobald devkitPro installiert ist: Es
überträgt die `.nro` direkt in hbmenu und leitet gleichzeitig stdout/stderr zurück an den
Entwicklungsrechner. Genau darauf ist die nxlink-Senke in `embedder/src/log.cpp` bereits
ausgelegt. Voraussetzung ist, dass Konsole und PC im selben Netz sind.

Alternative ohne Netzwerk: `.nro` auf die SD-Karte kopieren und das Logfile unter
`sdmc:/switch/flutter-libnx/` hinterher am PC auslesen. Langsamer, aber unabhängig.

## Offene Fragen ans Gerät

Diese Angaben werden gebraucht, bevor Hardwaretests sinnvoll geplant werden können:

1. Horizon-Firmwareversion (Systemeinstellungen → System)
2. Atmosphère-Version (hbmenu zeigt sie an)
3. IP-Adresse der Konsole im WLAN (hbmenu zeigt sie an)

Firmwareabhängig sind unter anderem die Medienspieler-Optionen des Web-Applets
(`BootAsMediaPlayer` ab 2.0.0, `MediaPlayerUi` ab 8.0.0 – siehe `docs/target-apps.md`)
und die Verfügbarkeit der CodeMemory-Syscalls ab 4.0.0, die für FFI-Callbacks relevant
werden könnten.
