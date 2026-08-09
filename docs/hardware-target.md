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

| Modus | Start über | Verfügbarer Speicher |
|---|---|---|
| Applet-Modus | Album-Symbol | **~440 MB** |
| Anwendungsmodus | Titelübernahme (R beim Starten eines Spiels halten) | **3 GB+** |

Für gewöhnliche Homebrew ist der Unterschied unerheblich. Für uns ist er es nicht:
Flutter Engine, Skia, Dart-Heap, Assets und mindestens zwei Framebuffer à 3,5 MB müssen
gemeinsam hineinpassen. 440 MB sind dafür knapp bis unrealistisch – zumal wir zu Beginn
nicht auf Speichereffizienz optimieren werden, sondern auf Korrektheit.

**Entscheidung für das MVP:** Zielumgebung ist der **Anwendungsmodus**. Der Applet-Modus
wird nicht zur Voraussetzung gemacht. Ob es später auch dort hineinpasst, ist eine
Optimierungsfrage und keine MVP-Frage.

Praktische Folge: Beim Testen muss die Titelübernahme verwendet werden. Läuft eine
Messung im Applet-Modus, sind die Speicherzahlen nicht vergleichbar und müssen als solche
gekennzeichnet werden.

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
