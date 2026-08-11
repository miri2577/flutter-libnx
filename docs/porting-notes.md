# Porting Notes

Fortlaufendes Protokoll technischer Befunde. Neueste Einträge oben.

Format je Eintrag: Befund · Beleg (Datei:Zeile oder Kommando) · Konsequenz.

---

## 2026-08-11 (Nacht, V) – Texteingabe und Dateiauswahl: der volle App-Zyklus steht

**Auf Hardware bestätigt: Rezept mit der Systemtastatur angelegt, in
sqlite gespeichert, Neustart überlebt.** Eine unveränderte Flutter-App
ist damit auf der Konsole voll benutzbar.

**Bildschirmtastatur** (`textinput_horizon.cpp`): Der Kanal
`flutter/textinput` spricht JSON, nicht den Standard-Codec. Drei Dinge,
die man wissen muss:

* `swkbdShow` blockiert die Hauptschleife - und damit die Engine. Das ist
  richtig so: Das Applet übernimmt den Bildschirm komplett; die Antwort
  auf `TextInput.show` geht deshalb VOR dem Öffnen raus.
* Flutter zählt Auswahl-Indizes in **UTF-16-Einheiten**, swkbd liefert
  UTF-8 - ohne Umrechnung steht der Cursor bei Umlauten/Emoji falsch.
* Abbruch = nichts senden. Das Framework behält den alten Text, wie bei
  einer weggewischten Handy-Tastatur.

**`file_picker`** ohne Datei-Browser: Die Konvention
`/switch/flutter_apps/<app-id>/import/` ersetzt die Ordnernavigation.
Einzelauswahl liefert die neueste Datei ("wer gerade etwas auf die Karte
kopiert hat, meint genau diese"), Mehrfachauswahl alle mit passender
Endung, leerer Ordner "abgebrochen" (null). Antwortformat des
MethodChannels: Liste von Maps {path, name, size, bytes: null,
identifier: null}.

Aufgeräumt: sqlite3-Rauchprobe stillgelegt (reaktivierbar), fstat-Diagnose
entfernt, Frame-Messung eingebaut (Zeile alle 600 präsentierte Frames -
feuert nur bei anhaltendem Zeichnen, im Ruhezustand rendert Flutter nicht).

---

## 2026-08-11 (Nacht, IV) – sqlite3 läuft: FFI ohne dlopen, und zwei devoptab-Fallen

**rezkonv_app läuft fehlerfrei mit Datenbank und FTS5-Volltextsuche.**
Der Weg dorthin war eine Kette aus fünf Befunden, jeder vom Log benannt:

1. **`Ffi_dl_providesSymbol` ohne Fallthrough-Schutz** – vierte stille
   Weiche, siehe Eintrag III. Danach Haken-Architektur: Engine fragt
   schwach gebundene `flutter_libnx_dl_open`/`_dl_sym`, der Embedder
   bedient sie aus Symboltabellen statisch gelinkter Bibliotheken.
2. **`package:ffi` löst malloc/free über den Prozess** – eigene kleine
   Prozess-Symboltabelle; deckt eine ganze Klasse von FFI-Paketen ab.
3. **`sqlite3_temp_directory` ist eine Variable, keine Funktion** – die
   nm-Tabelle muss Datensymbole (D/B/R/G) mitnehmen, nicht nur T.
4. **devoptab: Lesen ab/hinter EOF liefert -1 statt 0** – sqlite deutete
   das als "database disk image is malformed". `--wrap=read` stellt die
   POSIX-Semantik her (Gegenprobe über fstat+lseek nur im Fehlerfall).
5. **devoptab: `stat()` auf eine gerade geöffnete Datei scheitert mit
   EIO** – ebenso ein Zweit-`open` (der Dateidienst hält exklusiv).
   sqlites `getFileMode` fragt beim Journal-Anlegen genau so die Rechte
   der offenen DB ab (gemeldet als irreführendes `SQLITE_IOERR_FSTAT`).
   `--wrap=stat`: erst `__real_stat`, dann open+fstat-Gegenprobe, und im
   eindeutigen Belegt-Fall (stat **und** open beide EIO; nicht existent
   wäre ENOENT) die ehrliche Minimalantwort S_IFREG|0644.

Werkzeug des Tages: die **C-Rauchprobe** (`sqlite3_probe_horizon.cpp`) –
dieselbe Kette wie der Dart-Pfad, aber jeder Schritt einzeln geloggt, plus
Roh-I/O-Gegenprobe mit denselben POSIX-Aufrufen, die sqlites VFS benutzt.
Ohne sie hätte jede der fünf Schichten einen Hardware-Zyklus mehr gekostet.

Und eine Patch-Lektion: **Anker nie nur auf den Funktionskopf setzen.**
Der Stufe-1-Patch hat sich bei einem zweiten Lauf erneut eingefügt (der
Kopf passt immer) und den Hook-Block verdeckt - eine Stunde Fehlersuche
für ein Idempotenz-Detail. Anker müssen die erste Originalzeile des
Rumpfs einschließen.

Eingebaut, aber noch offen: FTS5 verlangte nur `-DSQLITE_ENABLE_FTS5`
("no such module: fts5"). Nächstes Paket: Bildschirmtastatur über das
swkbd-Applet an `flutter/textinput`.

---

## 2026-08-11 (Nacht, III) – Erste fremde App bedienbar; die vierte stille Weiche

**rezkonv_app läuft auf der Konsole und bleibt bedienbar.** Suchleiste,
Navigation, Einstellungen; die Datenbank fehlt noch und wird von der App
selbst als Fehler angezeigt – mit unserem eigenen Text darin.

Drei Stücke haben das möglich gemacht:

1. **StandardMessageCodec** (`standard_message_codec.cpp`) – die
   `plugins.flutter.io/*`-Kanäle sprechen das Binärformat, nicht JSON.
   Umgesetzt ist die Teilmenge null/bool/int/double/String/List/Map;
   typisierte Arrays lehnt der Decoder ehrlich ab. Zwei Formfallen:
   dreistufige Größenkodierung (254/255-Marker) und double-Ausrichtung
   auf 8 relativ zum Pufferanfang.
2. **`path_provider` + `shared_preferences`** (`plugins_horizon.cpp`) –
   Ablage unter `/switch/flutter_apps/<app-id>/`, absichtlich ohne
   `sdmc:`-Präfix (POSIX-reiner Pfad übersteht die Pfadarithmetik des
   Dart-path-Pakets; hbmenu startet mit sdmc als Standardgerät).
   Preferences als codec-kodierte Datei: Drahtformat = Dateiformat,
   kein eigener Parser, Ersetzen über Nebendatei + rename (FAT kann
   nicht atomar; schlimmster Fall ist "beginnt leer").
3. **Die vierte stille Plattformweiche**: In
   `Utils::Load/Resolve/UnloadDynamicLibrary` (`platform/utils.cc`)
   griff für Horizon **kein** `#if`-Zweig – Durchfallen ohne `return`,
   undefiniertes Verhalten. Auf Hardware: Data Abort mit `FAR = 0x48`
   in `DN_Ffi_dl_providesSymbol`, sobald `package:sqlite3` per
   `providesSymbol` den Prozess abfragte; die App schloss sich nach dem
   Startbildschirm. Der Absturzbericht plus `resolve-crash.sh` führte in
   Minuten zur Zeile. Patch `patch_dart_platform_utils_dynlib`:
   Fehlertext + nullptr. *Merksatz bestätigt: Bei jedem neuen
   Plattform-#if in fremdem Code zuerst fragen, was im Nicht-Treffer-Fall
   passiert.*

**Spezifikation für das nächste Paket, wörtlich aus dem Log:**
`package:sqlite3` ruft `DynamicLibrary.open('libsqlite3.so')`
(`load_library.dart:60`). Der Weg: sqlite3 statisch aus den
devkitPro-Portlibs linken und die dl-Funktionen für Horizon an eine
Embedder-Tabelle (oder dlsym-auf-sich-selbst über die eigene .dynsym)
anschließen – dann trägt derselbe Mechanismus jede statisch gelinkte
C-Bibliothek für FFI.

**Offen notiert:** Software-Renderer bei rezkonv "etwas ruckelig"
(Nutzerurteil, 720p, ein Thread). Erst messen (Frame-Zeiten loggen),
dann optimieren.

---

## 2026-08-11 (Nacht, II) – Platform Channels

**Auf Hardware bestätigt:** MethodChannel `flutter_libnx/system`, Dart fragt
den Batteriestand ab, der Embedder antwortet über `psm` – Wert stimmt.

Entscheidungen und Fallen:

* **JSONMethodCodec statt StandardMethodCodec.** Das Binärformat des
  Standards müsste der Embedder erst nachbauen; JSON ist eine Zeichenkette
  mit bekannter Form (`{"method":"...","args":...}`), die Antwort ein
  JSON-Array: `[ergebnis]` für Erfolg, `[code, meldung, details]` für
  Fehler. Der Codec wird auf der Dart-Seite am Kanal gesetzt.
* **Mit registriertem `platform_message_callback` beantwortet die Engine
  nichts mehr von selbst.** Jede Nachricht mit `response_handle` muss
  quittiert werden – auch fremde Kanäle wie `flutter/textinput` und
  `flutter/mousecursor`, die das Framework ungefragt schickt. Leere Antwort
  = „nicht implementiert" (MissingPluginException, auf die Dart-Code gefasst
  ist). Ein liegen gelassener Handle lässt das `await` für immer hängen.
* **`Random.secure()` war bisher tot:** `csrngGetRandomBytes` lief ohne
  `csrngInitialize` und antwortete `LibnxError_NotInitialized` → EIO. Jetzt
  als Init/Exit-Paar (`random_horizon.cpp`), wie `pl:u` und `psm` – der
  Wirtsprozess vergisst nichts.
* Der Snapshot wächst durch `package:flutter/services.dart` von 150.948 auf
  800.477 Zeilen Assembly – erwartbar, kein Fehlerbild.

---

## 2026-08-11 (Nacht) – Der Wirtsprozess vergisst nichts

Leitmotiv des Tages, in einem Satz: **Die NRO läuft im Prozess des Spiels,
und alles, was ein Lauf nicht selbst zurückbaut, erbt der nächste** – Heap-
Seitenzustände genauso wie Service-Sessions. Zwei Fehlerbilder, eine Wurzel.

### Erbschaftsbereinigung bestätigt

`flutter_libnx_heal_heap` (in `thread_diag_horizon.cpp`, Aufruf als Erstes in
`main()`) baut geerbte `svcMapMemory`-Löcher im Heap per Spiegel-Paarung
zurück. Sicherungen: eigener Main-Stack ausgenommen (Spiegel unter SP);
Paarung nur bei exakt gleicher Größe; Mesosphäre validiert die physische
Entsprechung, falsche Paarungen sind folgenlose Fehlversuche.

**Fünf Zyklen im selben Wirtsprozess, alle sauber gestartet und beendet** –
jeder ab dem zweiten mit genau einem zurückgebauten 1-MB-Erbstück (dem Stack
des letzten abgelösten Threads des Vorlaufs, den libnx' lazy-Reaper nie mehr
abräumt). Vorher starb verlässlich jeder zweite Start an `_malloc_r`
(`FAR = X3+8`) oder `threadCreate` (0xd401).

### Sessions leaken genauso: 2011-0102 nach dem dritten Zyklus

Neues Fehlerbild, nachdem die Heap-Erbschaft geheilt war: Systemabsturz
**nach** sauberem App-Abgang („ui_app beendet" noch im Log), deterministisch
beim jeweils dritten Lauf im selben Prozess, zwei Sitzungen lang. Fehlercode
`2011-0102 (0xcc0b)` = Modul 11 (HIPC), Beschreibung 102 – **out of
sessions**.

Täter: `fonts_horizon.cpp` rief `plInitialize` je Lauf, `plExit` existierte
nicht – eine pl:u-Session samt gemappter Shared-Font-Memory je Zyklus. Dazu
`romfsInit` ohne `romfsExit`. Nach drei Zyklen war das Limit erschöpft; das
nächste, was im Prozess eine Session brauchte (hbmenu-Reload nach unserem
Abgang), starb – deshalb der Absturz *nach* uns, nicht *in* uns.

Reparatur: `flutter_libnx_fonts_cleanup()` (plExit) + `romfsExit()` nach dem
Engine-Shutdown, vor dem Log-Abbau. Mit beiden Reparaturen zusammen: die
fünf sauberen Zyklen oben, wo vorher bei drei Schluss war.

**Merksatz für alle weiteren Dienste:** Auf dieser Plattform ist jedes
`xyInitialize` ohne sein `xyExit` kein Schönheitsfehler, sondern eine
tickende Uhr im Wirtsprozess. Bei neuen Diensten (`csrng` ist so ein
Kandidat – `csrngGetRandomBytes` läuft bisher ohne explizites
`csrngInitialize`/`csrngExit`) sofort das Paar anlegen.

---

## 2026-08-11 (Abend) – Controller gelöst, Absturzursache gefunden

### Controller-Eingabe: zwei stumme Verwerfungen hintereinander

**Auf Hardware bestätigt: Steuerkreuz, Stick und A wirken.** Der Weg dorthin
deckte zwei getrennte Fehler auf, beide vom selben Schlag wie die drei stummen
Kanäle vom 2026-08-10 – *ein Ereignis wird verworfen, der Absender bekommt
Erfolg gemeldet*.

**1. Das View-Fokus-Ereignis kam zu früh.** Der Empfänger ist das
`View`-Widget aus `runApp()` (`widgets/view.dart:241`, über
`WidgetsBindingObserver.didChangeViewFocus`). Gesendet wurde direkt nach
`RunInitialized` – und weil Plattform- und UI-Runner hier derselbe Thread
sind, stellt `Shell::OnPlatformViewSendViewFocusEvent` (`shell.cc:2306`) über
`RunNowOrPostTask` *sofort* zu, bevor die Hauptschleife die Startaufgaben des
Isolates abgearbeitet hat. Dann verwirft
`RuntimeController::SendViewFocusEvent` (`runtime_controller.cc:216`) das
Ereignis mit stillem `return false` – keine Pufferung, und der
Embedder-Aufruf meldet trotzdem `kSuccess`. **Konsequenz:** Das Ereignis wird
jetzt erst nach dem ersten präsentierten Frame gesendet (Flag im
Present-Callback); dann hat der Widget-Baum sicher gebaut. Richtung
`kForward`, damit `findFirstFocus` deterministisch das erste fokussierbare
Widget nimmt.

**2. Reine KeyData-Ereignisse dispatcht das Framework nicht.** Der
`KeyEventManager` arbeitet im Modus `keyDataThenRawKeyData`
(`hardware_keyboard.dart:1082-1113`): Nicht-synthetische Ereignisse aus
`FlutterEngineSendKeyEvent` landen in der Warteschlange
`_keyEventsSinceLastMessage` und werden erst zugestellt, wenn die zugehörige
*Rohnachricht* auf dem Kanal `flutter/keyevent` eintrifft. Die
Desktop-Embedder senden deshalb immer beides als Paar. Wir sendeten nur
KeyData – die Ereignisse stauten sich für immer, `handled=0` bei stehendem
Fokus (Bildschirmanzeige: `focusNode (Kontext: true)`). **Konsequenz:**
`synthesized=true` nimmt die dokumentierte Abkürzung – synthetische
Ereignisse bei leerer Warteschlange werden sofort über `handleKeyEvent` +
`keyMessageHandler` zugestellt (voller Widget-Pfad, Fokus, Shortcuts,
Actions). **Preis: Die `handled`-Rückmeldung an den Embedder ist seitdem
prinzipbedingt immer `false` und taugt nicht mehr als Diagnosesignal.** Wer
sie braucht, muss das Nachrichtenpaar der Desktop-Embedder nachbauen
(GLFW-Keymap auf `flutter/keyevent`).

### Sporadischer Absturz: die NRO erbt einen gebrauchten Heap

**Der echte Fehlercode statt ENOMEM** (über `thread_diag_horizon.cpp`):

```text
threadCreate: 0x0000d401 = Modul 1, Beschreibung 106
            = KernelError_InvalidMemoryState
```

Kein Speichermangel – der von `memalign` gelieferte Stack-Block enthielt
Seiten, deren Memory-State `svcMapMemory` nicht zulässt.

**Der Heap-Scanner** (`flutter_libnx_scan_heap` in `thread_diag_horizon.cpp`,
Kontrollpunkte in `main()`) hat den Unterschied zwischen Erfolgs- und
Fehlschlagsläufen gefangen:

| Lauf | Beim Eintritt in `main()` |
|---|---|
| Erfolg | 1 geliehene Region (2256 KB, immer, gleiche Adresse) |
| Fehlschlag | dieselbe **plus ein 1-MB-Loch** (`attr=IsBorrowed`, `perm=None`) |

Das Loch existiert, *bevor* unser Code etwas getan hat. Erklärung: Die NRO
läuft im Prozess des Spiels, hbloader übergibt den Heap „gebraucht" – die
Kernel-Zustände der Seiten überleben den NRO-Wechsel. Ein *erfolgreicher*
Lauf hinterlässt beim Abgang mindestens einen noch gemappten Thread-Stack;
der nächste Start im selben Prozess hält diese Seiten für freien Speicher.
Beide Symptome folgen aus demselben Loch:

* Freilisten-Kette läuft hinein → Lesen von `next` bei +8 in unlesbarer
  Region → Data Abort in `_malloc_r` mit `FAR = X3 + 8`
* `memalign` reicht das Loch an `threadCreate` weiter → `svcMapMemory` →
  `InvalidMemoryState` (0xd401)

**Die Abwechslung „jeder zweite Start" ist damit erklärt:** Ein Absturz
reißt den Wirtsprozess mit (nächster Start: frischer Heap → Erfolg), ein
Erfolg hinterlässt Löcher (nächster Start im selben Prozess: Fehlschlag).
Die Vorhersage „nach Absturz gelingt der nächste Lauf" ist auf Hardware
eingetroffen.

**Offen:**

* Erbschaftsbereinigung beim Start: geerbte Löcher per `svcUnmapMemory`
  zurückbauen. Der Kernel validiert das Paar (dst, src, size); Fehlversuche
  bei der Paarungssuche sind harmlos.
* Eigener Abgang ohne Hinterlassenschaft: Welcher Thread bleibt nach
  `FlutterEngineShutdown` gemappt? (Scan vor dem Beenden einbauen.)
* Die konstante 2256-KB-Region stammt nicht von uns (schon im frischen
  Prozess da, immer gleiche Adresse) – mutmaßlich hbmenu/hbloader. Solange
  sie *ein* Block bleibt, den `memalign` nie erwischt hat, ist sie harmlos;
  die Bereinigung sollte sie trotzdem mitnehmen.

---

## 2026-08-11 – Stand bei Sitzungsende

**Zwei getrennte Fehler sind offen, beide gut eingegrenzt.**

### 1. Sporadischer Absturz in `_malloc_r`

Tritt bei `FlutterEngineRunInitialized` auf, etwa jeder zweite Lauf. Über
mehrere Berichte hinweg **dasselbe Muster**:

```text
PC, LR  →  _malloc_r
FAR = X3 + 8
```

Die angefasste Adresse liegt konstant acht Bytes hinter dem Wert in X3 – das
Bild eines Freilisten-Zeigers, dessen Nachfolgerfeld ins Leere zeigt. Dass es
über verschiedene Läufe hinweg gleich aussieht, spricht gegen Zufall und für
eine Beschädigung der Heap-Verwaltung.

Ausgeschlossen ist inzwischen:

| Verdacht | Befund |
|---|---|
| Speichermangel | `thread_probe`: 64 Threads à 1 MB gehen mühelos, Zahlen rühren sich nicht |
| Heap defekt übergeben | Heap-Probe beim Start: 256 MB am Stück nutzbar |
| Zu große Thread-Stacks | halbiert, ohne Wirkung |
| Zu viele Engine-Threads | zusammengelegt, ohne Wirkung |
| Überlauf eigener VM-Blöcke | Wächter in `virtual_memory_horizon.cc` schweigen |

Der Schaden entsteht also **zwischen** dem Programmstart (Heap noch intakt) und
dem VM-Start. Nicht geprüft sind bisher: `romfsInit`, `framebufferCreate`
(Schattenpuffer) und die statischen Konstruktoren.

### 2. Controller-Eingabe ohne Wirkung

Beweiskette vollständig, Ursache benannt, Reparatur **gebaut aber ungetestet**
(der Lauf stürzte vorher an Fehler 1 ab):

- Ereignisse erreichen das Framework – 72 gesendet, 72 Rückmeldungen
- Keines wird beansprucht – `handled=0`, auch bei Enter
- Touch funktioniert, weil Zeiger keinen Fokus brauchen
- **Das Framework hält die View für unfokussiert**, bis der Embedder das
  Gegenteil meldet

Eingebaut, noch nicht auf Hardware bestätigt:
`FlutterEngineSendViewFocusEvent` mit `kFocused` (`embedder.h:2997`), dazu die
Lebenszyklus-Nachricht `AppLifecycleState.resumed`. Beides ist nötig: Die eine
sagt „die Anwendung ist aktiv", die andere „diese View hat den Eingabefokus".

### Werkzeuge, die bereitstehen

| Werkzeug | Zweck |
|---|---|
| `examples/thread_probe` | Threads und Heap messen, ohne Engine – baut in Sekunden |
| `embedder/src/platform/crash_handler_horizon.cpp` | Absturzbericht mit PC/LR/FAR und Bezugspunkt |
| `build-logs/resolve-crash.sh` | Adressen in Quellzeilen auflösen |
| `embedder/src/platform/thread_diag_horizon.cpp` | echter Result-Code bei Threadfehlern |
| `TRACE=1 scripts/patch-engine-horizon.py` | Startmarken ein-/ausschalten |
| `scripts/rebuild-all.sh` | ganze Kette in richtiger Reihenfolge |

---

## 2026-08-11 – Gemessen statt erschlossen: `ENOMEM` bedeutet gar nichts

**Ein Messprogramm ohne Engine** (`examples/thread_probe`, 277 KB, Bauzeit
Sekunden) hat die Grundlage der letzten Stunden widerlegt.

```text
[Start]   gesamt 3189 MB, Heap 3183 MB, ausserhalb 5 MB
pthread_create,     1 MB Stack:  64 Threads erzeugt   (alle, kein Fehler)
libnx threadCreate, 1 MB Stack:  64 Threads erzeugt   (alle)
Heap danach:                     bis 256 MB am Stueck nutzbar
```

**Beide Wege schaffen 64 Threads mit je 1 MB Stack, und die Speicherzahlen
bewegen sich dabei um kein Megabyte.** Damit ist widerlegt:

| Annahme | Befund |
|---|---|
| „rund 20 MB außerhalb des Heaps sind die Grenze" | die Zahl ändert sich auch bei 64 Threads nicht |
| „2 MB Stack je Thread sind zu viel" | 64 × 1 MB gehen mühelos |
| „`ENOMEM` heißt, der Speicher ist alle" | **falsch, siehe unten** |

**Der eigentliche Fund** steht in `nx/source/runtime/newlib.c:209-213`:

```c
_error2:
    threadClose(&t->thr);
_error1:
    __libnx_free(t);
    return ENOMEM;      // immer, egal was scheiterte
```

`__syscall_thread_create` verwirft den Result-Code und meldet für **jeden**
Fehlschlag `ENOMEM` – ob `threadCreate`, `svcSetThreadCoreMask` oder
`threadStart` gescheitert ist. Die Meldung

    Could not start thread dart:io EventHandler: 12 (Not enough space)

sagt also nichts über die Ursache. Alle daraufhin gebauten Reparaturen –
kleinere Stacks, Heap-Reserve, Heap-Verkleinerung, zusammengelegte
Task-Runner – zielten auf eine Diagnose, die es nie gegeben hat.

*Dieselbe Falle wie beim Zähler in `relink-example.sh`, der nur eine Fehlerart
kannte und bei jeder anderen Erfolg meldete. Eine Fehlermeldung ist erst dann
eine Information, wenn belegt ist, dass sie zwischen Fällen unterscheidet.*

**Ein eigener Messfehler gehört dazu:** Der erste Durchlauf meldete 31 pthreads
statt 64 – weil die zuvor erzeugten 64 libnx-Threads noch liefen. Proben
müssen ihren Zustand hinterher aufräumen, sonst misst die nächste nur den Rest.

**Was offen bleibt:** Warum der Thread in der Anwendung scheitert, ist damit
wieder unbekannt – nur Speicher und Threadanzahl sind ausgeschlossen. Der
nächste Schritt ist, den echten Result-Code sichtbar zu machen, statt ihn sich
von der pthread-Schicht verschlucken zu lassen.

---

## 2026-08-11 – Task-Runner zusammengelegt: Eingabe gewonnen, Speicher nicht

**Ausgangslage:** Zwei getrennte Probleme, für die derselbe Umbau in Frage kam.

1. `Could not start thread …: 12 (Not enough space)` – der Prozess hat
   außerhalb des Heaps nur rund 20 MB für Thread-Stacks und deren
   Spiegelabbildungen.
2. Controller-Eingabe ohne Wirkung: 70 Tastenereignisse mit `kSuccess`
   gesendet, **keine einzige Rückmeldung**.

**Der Umbau:** `FlutterCustomTaskRunners` mit demselben Runner für Plattform-,
Render- und UI-Aufgaben (`embedder/src/task_runner.cpp`). Die Engine erzeugt
dafür keine eigenen Threads mehr; die Hauptschleife arbeitet die Aufgaben mit
`FlutterEngineRunTask` ab.

**Was er gebracht hat – Punkt 2 ist gelöst:** 44 gesendete Tastenereignisse,
**44 Rückmeldungen**. Vorher null. Die Ursache war, dass die Engine auf dem
aufrufenden Thread eine Nachrichtenschleife erwartet; wer dort eine eigene
betreibt, ohne ihre Aufgaben abzuholen, bekommt nie eine Antwort. Rendering und
Touch funktionierten trotzdem, weil sie über UI- und Rasterthread liefen – ein
Grund, warum das lange unbemerkt blieb.

*Der Fehler war damit von „reagiert nicht" auf „kommt an, wird nicht
beansprucht" eingegrenzt: Alle Rückmeldungen lauten `handled=0`, auch bei
Enter. Es hält also nichts den Fokus.*

**Was er nicht gebracht hat – Punkt 1 besteht fort:** Der `ENOMEM`-Fehler trat
erneut auf. Im Nachhinein einleuchtend: Der scheiternde Thread gehört nicht der
Engine, sondern **Dart** (`dart:io`-Eventhandler). Drei Engine-Threads
einzusparen hilft, reicht aber nicht – Dart legt weiterhin eigene an.

**Damit sind alle bequemen Hebel ausgeschöpft:**

| Ansatz | Ergebnis |
|---|---|
| Stackgröße halbieren (2 → 1 MB) | reicht nicht |
| `__libnx_initheap` mit Reserve | wirkungslos, Loader gibt Heap vor |
| Heap per `svcSetHeapSize` verkleinern | **zerstört die Anwendung** – hbloader lädt die NRO in diesen Heap |
| Engine-Threads zusammenlegen | reicht nicht, Dart-Threads bleiben |

Der Engpass ist strukturell: Der Homebrew-Loader gibt über 99 % des Speichers
als newlib-Heap, und was der Kernel für Abbildungen braucht, liegt außerhalb
davon. Der nächste Ansatzpunkt wäre, hbloader zu einem kleineren Heap zu
bewegen (NACP/Loader-Konfiguration) oder zu klären, ob der Fehlschlag aus
`__libnx_aligned_alloc` oder aus `svcMapMemory` stammt – das ist bisher nicht
gemessen, sondern erschlossen.

---

## 2026-08-11 – Nachtrag: Der Eintrag unten greift zu kurz

**Der Titel „Es war kein beschädigter Heap, sondern ein zu großer" ist nicht
haltbar.** Der `ENOMEM`-Befund beim Thread-Start stimmt und ist belegt; die
Schlussfolgerung, damit sei die Sache erklärt, war voreilig.

Nach der Halbierung der Thread-Stacks (2 MB → 1 MB) trat derselbe Absturz
erneut auf – und zwar **früher als je zuvor**: zwischen zwei Logzeilen in
`main()`, lange vor `FlutterEngineInitialize`. Dazwischen liegt nur ein
`appletGetAppletType()` und die Logausgabe selbst, die eine kleine Allokation
vornimmt. PC und LR wieder in `_malloc_r`.

An dieser Stelle existiert genau **ein** Thread. Ein Mangel an Speicher für
Thread-Stacks kann es dort nicht sein. Es bleiben zwei Möglichkeiten:

1. Der Heap ist zu diesem Zeitpunkt bereits beschädigt – durch etwas, das
   vorher lief: statische Konstruktoren, `romfsInit`, `LogInit`,
   `framebufferCreate`/`framebufferMakeLinear`.
2. Der vom Loader übergebene Heap ist nicht in dem Umfang nutzbar, den
   `fake_heap_end - fake_heap_start` angibt.

**Was das für die Stack-Halbierung heißt:** Sie bleibt richtig – 20 MB
außerhalb des Heaps sind für ein Dutzend Threads zu wenig, und der
`ENOMEM`-Fehler war echt. Sie behebt aber offenbar nicht die Ursache der
sporadischen Abstürze.

**Und für `heap_layout_horizon.cpp`:** Die Datei ist im Normalfall
wirkungslos, weil der Loader den Heap vorgibt. Sie bleibt für den Fall ohne
Override, ist aber kein Beitrag zur Lösung.

*Selbstkritik, die hierher gehört:* Der Eintrag unten wurde geschrieben,
während ein einzelner Lauf durchlief. Genau davor warnt ein früherer Eintrag
in dieser Datei – „ein sauberer Durchlauf beweist hier nichts". Ich habe die
eigene Regel nicht angewandt.

---

## 2026-08-11 – Es war kein beschädigter Heap, sondern ein zu großer

> **Eingeschränkt, siehe Nachtrag darüber.** Der `ENOMEM`-Befund stimmt, die
> Schlussfolgerung „damit ist es erklärt" nicht.

**Befund:** Nach dem Rückbau des Wächterwerkzeugs kam endlich eine eindeutige
Meldung:

```text
runtime/bin/thread.cc:19: error: Could not start thread dart:io EventHandler:
    12 (Not enough space)
```

12 ist `ENOMEM`. **Es war nie eine Speicherbeschädigung – der Speicher ging
aus.** Damit fügen sich alle Beobachtungen zusammen: die sporadischen Abstürze
an wechselnden Stellen, der Data Abort in `_malloc_r`, das Verschwinden des
Fehlers nach einer BSS-Vergrößerung. Ein erschöpfter Heap sieht je nach
Zeitpunkt anders aus, und `malloc` ist die Stelle, an der es am ehesten
auffällt.

**Die Ursache steht in libnx** (`nx/source/runtime/init.c:78`):

```c
if (mem_available > mem_used+0x200000)
    size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
```

Der newlib-Heap bekommt **alles bis auf 2 MB**. Für gewöhnliches Homebrew ist
das richtig – es alloziert über malloc und sonst nichts. Die Flutter-Engine tut
das Gegenteil: Sie legt ein gutes Dutzend Threads an, und libnx spiegelt jeden
Stack zusätzlich per `svcMapMemory` an eine zweite Adresse
(`nx/source/kernel/thread.c:132`). Diese Abbildungen brauchen Speicher, der
**nicht** im Heap liegen darf.

**Warum der Anwendungsmodus nicht genügte:** Der knappe Anteil wächst nicht
mit. Gemessen wurden 3189 MB gesamt – davon 3168 MB Heap und **20 MB** für
alles andere. Bei 2 MB je Stack sind das etwa zehn Threads, und genau an dieser
Grenze entscheidet sich jeder Start.

**Ein Irrweg, der festgehalten gehört:** Der erste Reparaturversuch war eine
eigene Fassung von `__libnx_initheap` mit größerer Reserve. Sie hat nie
gewirkt – der Homebrew-Loader gibt den Heap vor (`envHasHeapOverride()`), und
damit wird der ganze Rechenweg übersprungen. Aufgefallen ist es nur, weil die
Zahlen nicht zur Erwartung passten: 20 statt 256 MB. Die Protokollzeile sagt
seither ausdrücklich, woher die Aufteilung stammt.

*Regel daraus:* Wer eine Voreinstellung ändert, muss prüfen, ob sein Code sie
überhaupt trifft. Ein Lauf, der danach durchläuft, beweist nichts – hier wäre
sonst eine wirkungslose Änderung als Reparatur in die Ablage gewandert.

**Was tatsächlich wirkt:** Den Heap nachträglich zu verkleinern verbietet sich,
weil hbloader die NRO selbst hineinlädt. Der Hebel liegt auf der anderen Seite
der Rechnung – `fml::Thread::GetDefaultStackSize()` liefert 2 MB, für Horizon
jetzt 1 MB. Damit passen doppelt so viele Threads in denselben Rest. Der Wert
ist nicht gegriffen: Dart gibt seinen eigenen Threads über
`OSThread::GetMaxStackSize` ebenfalls 1 MB.

**Zur Fehlleitung durch die eigene Messung:** Die Zeile
„3185 von 3189 MB belegt" wurde lange als Auslastung gelesen. Sie ist die
Heap-*Reservierung*. Wer sie so liest, sucht den Fehler dort, wo kein Speicher
verbraucht wird. Die Zeile nennt jetzt Heapgröße und verbleibenden Rest
getrennt.

---

## 2026-08-11 – Der Heap-Wächter war ein Fehlschlag, und warum

**Vorhaben:** Jede Allokation über `-Wl,--wrap=malloc` einfassen – Kopf mit
Größe und Aufrufer davor, Wächterbytes dahinter –, um die Speicherbeschädigung
zu finden, die sich als Data Abort in `_malloc_r` zeigt.

**Ergebnis: nichts gefunden, zwei eigene Fehler erzeugt.** Der Ansatz ist so
nicht brauchbar, und die Gründe sind allgemeiner Natur:

**1. `--wrap` erfasst nicht, was innerhalb von libc alloziert.** Der Schalter
leitet nur Aufrufe um, die der Linker auflöst. `strdup`, `fopen`, `getcwd` und
Verwandte rufen `_malloc_r` innerhalb derselben Bibliothek – daran kommt der
Wrapper nicht heran. Ihre Blöcke haben also keinen Kopf, ihr `free()` landet
aber sehr wohl beim Wrapper. Sichtbar wurde das als

```text
FREE eines fremden Blocks 0x14f9d8d410 (magic=0x00000031)
```

und die Blöcke wurden nicht freigegeben – ein selbst gebautes Speicherleck.

**2. Ein verschobener Nutzzeiger bricht `malloc_usable_size`.** Der zweite
Absturz saß in `_malloc_usable_size_r`: Die Funktion liest newlibs
Verwaltungskopf unmittelbar vor dem übergebenen Zeiger und fand dort unseren.
Skia und Dart fragen die nutzbare Blockgröße ab, also trifft es sie sofort.

**3. Vorher schon:** `memalign` muss mitgewrappt werden, sobald `free` es ist –
die Dart-VM alloziert damit ihren gesamten Heap. Ohne das las `free()` einen
Kopf, den es nie gab.

**Was ein zweiter Anlauf erfüllen müsste:**

- Der zurückgegebene Zeiger bleibt **unverändert** der von malloc. Wächter nur
  *hinter* dem Nutzbereich, Größen in einer eigenen Tabelle statt in einem
  Kopf davor.
- `free()` reicht unbekannte Blöcke **durch** an `__real_free`, statt sie zu
  verwerfen.
- Alle Allokatoren derselben Familie zusammen wrappen (`malloc`, `calloc`,
  `realloc`, `memalign`, `free`) – oder keinen.

**Die eigentliche Lehre:** Ein Diagnosewerkzeug, das tiefer eingreift als der
gesuchte Fehler, produziert Befunde über sich selbst. Beide Abstürze dieses
Anlaufs waren meine, nicht die der Portierung – und der zweite sah dem
gesuchten zum Verwechseln ähnlich, weil er ebenfalls in der Speicherverwaltung
landete. Ohne den Bezugspunkt im Absturzbericht wäre das kaum auseinanderzu-
halten gewesen.

*Unverändert offen bleibt der ursprüngliche Befund:* ein Data Abort in
`_malloc_r` **ohne** jedes Wächterwerkzeug, aufgetreten vor diesem Anlauf.

---

## 2026-08-11 – Touch läuft, und ein Fehler verschiebt sich statt zu verschwinden

**Befund 1 – Touch trägt.** Berührungen werden als `FlutterPointerEvent`
zugestellt, ein `FilledButton` reagiert und zählt hoch. Der eigentliche Aufwand
lag nicht im Auslesen, sondern in der Zustandsverfolgung: Der Touchscreen
meldet, welche Finger *gerade* aufliegen, Flutter erwartet `kDown` → `kMove`* →
`kUp`. Wer die Momentaufnahme ungefiltert weiterreicht, erzeugt bei jedem Frame
ein neues `kDown` – die Berührung endet nie, und keine Schaltfläche reagiert.
Verglichen wird über `finger_id`.

Zwei Details, die man übersieht: `buttons` muss gesetzt sein, solange der
Finger aufliegt (sonst ist `kMove` eine Bewegung ohne Kontakt, Wischen
funktioniert nicht), und der Zeitstempel gehört in Mikrosekunden aus
`FlutterEngineGetCurrentTime()`, damit die Geschwindigkeitsberechnung für
Gesten stimmt.

Als Prüfstein bewusst ein `FilledButton` statt eines `GestureDetector`: Er
durchläuft Treffererkennung, Gestenerkennung, Zustandswechsel und Neuzeichnen.
Ein Pfad, bei dem nur `kDown` ankommt, würde damit auffallen.

**Befund 2 – der Ausnahmebehandler hatte selbst zu wenig Stack.** Der erste
Touch-Absturz hinterließ *keinen* Bericht, obwohl der Behandler eingebunden
war. libnx gibt ihm 1 KB (`nx/source/runtime/init.c:28`, schwach gebunden);
unser Behandler formatiert darin über ein Dutzend Zeilen, und der Logger legt
seinen Puffer ebenfalls dort ab. Er ist beim Berichten überlaufen. Jetzt 16 KB.

*Dasselbe Muster wie bei den drei stummen Meldewegen, eine Ebene tiefer: Nicht
das Programm schwieg, sondern das Werkzeug, das es zum Sprechen bringen sollte.*

**Befund 3 – und der wiegt am schwersten.** Nach der Stack-Vergrößerung ist der
Touch-Absturz **weg**. Die einzige Änderung war ein größeres globales Array.
Das kann keinen Fehler im Eingabepfad reparieren – es verschiebt die
Speicherlage.

Zusammen mit dem Data Abort in `_malloc_r` ergibt das eine Erklärung für
alles bisher Beobachtete:

> Es gibt **eine** Speicherbeschädigung. Sporadische Abstürze an wechselnden
> Stellen, der Absturz in malloc und ein Absturz beim Antippen, der nach einer
> BSS-Änderung verschwindet, sind keine unabhängigen Fehler, sondern
> Ausprägungen desselben – je nachdem, was zufällig neben dem beschädigten
> Bereich liegt.

**Eingegrenzt ist es damit auf gewöhnliche `malloc`-Blöcke:** Die Wächter um
die Blöcke der Dart-VM haben in keinem Lauf angeschlagen. Die großen,
ausgerichteten Allokationen sind also nicht betroffen.

*Konsequenz für die Bewertung künftiger Läufe:* Ein sauberer Durchlauf beweist
hier nichts. Solange die Ursache nicht gefunden ist, kann jede Änderung an
Größen, Reihenfolgen oder Puffern das Symptom verschieben – in beide
Richtungen.

---

## 2026-08-11 – Der Absturzbericht trägt: es ist `_malloc_r`

**Befund:** Erster Absturz, der einen verwertbaren Bericht hinterlassen hat.
Der Ausnahmebehandler aus `crash_handler_horizon.cpp` hat gegriffen:

```text
=== ABSTURZ: Data Abort o.ae. (error_desc=0x101) ===
  PC  = 0x1bef3149b8
  LR  = 0x1bef3147fc
  FAR = 0x5b943f4048   (angefasste Adresse)
  Bezugspunkt: __libnx_exception_handler laeuft auf 0x1bee8386f0
```

Aufgelöst über die Modulbasis (`Laufzeitadresse − ELF-Adresse des Handlers`,
Skript: `build-logs/resolve-crash.sh`): **PC und LR liegen beide in
`_malloc_r`** – newlibs Speicherverwaltung.

Damit ist der Fehler zum ersten Mal benannt. Ein Data Abort *in* malloc heißt
in aller Regel, dass dessen Verwaltungsstrukturen beschädigt sind: Der Schaden
entsteht früher als der Knall, und wo es knallt, hängt davon ab, welche
Allokation als erste über die kaputte Stelle stolpert. **Das erklärt die
Sporadik und die wechselnden Absturzstellen** der vergangenen Läufe besser als
jede bisherige Vermutung.

Passend dazu die Register: `FAR` liegt vier Bytes hinter `X3` – das Bild eines
Freilisten-Zeigers, der ins Leere zeigt.

**Was damit *nicht* geklärt ist:** wer den Heap beschädigt. Geprüft und
ausgeschlossen ist der naheliegendste Verdächtige, der Destruktor in
`virtual_memory_horizon.cc`: Er gibt `reserved_.pointer()` frei, und weil
`FreeSubSegment()` ehrlich `false` meldet, lässt `Truncate()` `reserved_`
unverändert – der Zeiger bleibt also der, den `memalign` geliefert hat.

**Beobachtung, die die Suche eingrenzt:** Mit der reinen `dart:ui`-Anwendung
lief es über viele Läufe stabil, mit dem Framework kracht es früh. Das
Framework alloziert um Größenordnungen mehr. Ein Schaden, der lange unentdeckt
bleibt, wird damit wahrscheinlicher gefunden – es muss also nicht am Framework
selbst liegen.

**Werkzeug für den nächsten Schritt:** `build-logs/resolve-crash.sh` nimmt den
Bezugspunkt und beliebig viele Adressen aus dem Bericht und gibt Funktion und
Quellzeile aus. Der Fatal-Screen der Konsole bleibt unbrauchbar – seine
Startadresse passt nicht zur NRO.

---

## 2026-08-11 – `runApp()` läuft, und zwei Dinge standen im Weg

**Ergebnis:** Eine gewöhnliche Flutter-Anwendung läuft auf der Konsole –
`MaterialApp`, `Scaffold`, Theme aus `ColorScheme.fromSeed`,
`AnimationController` am Ticker, Material-Widgets, Icon-Schriftart. Damit ist
der Weg von `dart:ui` zum vollständigen Framework offen.

### Paketauflösung

`dart:ui` steckt in der Plattform-Dill und ist ohne Weiteres auffindbar;
`package:flutter` liegt als Quellpaket im SDK und wird nur über
`.dart_tool/package_config.json` gefunden. Statt die Datei von Hand zu
schreiben, erzeugt ein minimales `pubspec.yaml` in `examples/ui_app/dart` sie
über `flutter pub get`. `gen_kernel` bekommt sie über `--packages`.

Größenordnung: Kernel 5 → 22 MB, Assembly 150.948 → 744.683 Zeilen, NRO
14,2 → 16,8 MB.

*Falle, die dabei entschärft wurde:* `build-ui-app.ps1` erzeugte die Assembly
noch mit dem `gen_snapshot` aus den Android-Artefakten – genau dem, der seit
dem Hash-Befund nicht mehr passt. Das Skript erzeugt jetzt nur noch den Kernel
und verweist auf `rebuild-all.sh`.

### `defaultTargetPlatform` kennt "horizon" nicht

```text
Unhandled Exception: Unknown platform.
horizon was not recognized as a target platform.
#0  defaultTargetPlatform (package:flutter/src/foundation/_platform_io.dart:39)
#3  new FocusManager (…/focus_manager.dart:1651)
#5  WidgetsBinding.initInstances (…/binding.dart:475)
```

Die Engine lief, die Isolate lief, der Bildschirm blieb schwarz: Das Framework
scheiterte beim Aufbau des `FocusManager`. Der vorgesehene Ausweg
`debugDefaultTargetPlatformOverride` greift nur unter `kDebugMode` – wir bauen
Product.

**Konsequenz:** `Platform::OperatingSystem()` meldet für Horizon `"linux"`
(`runtime/bin/platform.h`). Bewusst **nicht** geändert wurde
`kHostOperatingSystemName` selbst – der Name steht im Merkmalsstring des
Snapshots und in `target_abi_name`, dort ist "horizon" richtig. Geändert wird
nur, was die Dart-Anwendung sieht.

`"linux"` ist dabei die ehrlichste der auswählbaren Antworten: Die Portierung
bildet Horizon ohnehin durchgängig auf den POSIX-Zweig ab, und
`TargetPlatform.linux` bedeutet im Framework „Desktop-artig" – Fokus über
Tastatur und Steuerkreuz statt mobiler Gestenannahmen, was für eine Konsole
mit Controller passt.

### Der Applet-Modus ist zu klein – und das erklärt mehr als diesen Fehler

Mit dem Framework schlug `pthread_create` fehl
(`FML_CHECK` in `fml/thread.cc:80`); jeder Engine-Thread will 2 MB Stack, und
libnx holt sie aus dem Prozess-Heap. Die Messung direkt vor
`FlutterEngineInitialize`:

| Startweg | Applet-Typ | Prozessspeicher |
|---|---|---|
| Album (Applet) | 2 | **380 MB** |
| Spiel + R (Anwendung) | 0 | **3189 MB** |

**Damit ist eine Zahl aus Meilenstein 1 zu korrigieren:** Dort steht „3007 MB
gesamt, 243 MB belegt – und zwar im Applet-Modus". Der Applet-Modus hat
tatsächlich 380 MB; die damalige Messung muss eine andere Größe erfasst haben.

Zur Lesart der Zahlen: `InfoType_UsedMemorySize` meldet im Anwendungsmodus
3185 von 3189 MB als belegt. Das ist **keine** Auslastung, sondern libnx'
Heap-Reservierung – der Heap ist da und weitgehend leer. Aussagekräftig für
freien Platz ist er nicht.

**Der Verdacht, der sich damit aufdrängt:** Die sporadischen Abstürze vom
Vortag könnten Speichermangel gewesen sein. Alle Läufe liefen im Applet-Modus,
und 137 MB frei müssen für Engine-Threads, Dart-Heap, Skia-Puffer und
Framebuffer reichen – mal geht es aus, mal nicht, an wechselnden Stellen.
Belegt ist das nicht, es passt nur auffällig gut. Der Absturzbericht steht
bereit, falls es im Anwendungsmodus wiederkommt.

**Praktische Folge für alles Weitere:** Getestet wird im Anwendungsmodus
(hbmenu über ein Spiel mit gehaltener R-Taste starten). Für Referenz-App als
Zielfall ist das ohnehin die einzig sinnvolle Betriebsart – Videopuffer
brauchen mehr als 137 MB.

---

## 2026-08-11 – Korrektur: Die Marken waren nicht die Ursache

**Der Eintrag unten ist in seiner Schlussfolgerung falsch.** Er stellt fest,
mit Debug-Marken laufe der Start durch und ohne sie nicht, und leitet daraus
ein Zeitverhalten ab. Grundlage waren zwei Läufe – einer mit, einer ohne.

Ein dritter Lauf ohne Marken lief vollständig durch: `kSuccess`, alle sechs
Schriften, Hauptschleife. Einzige Änderung seither ist der Absturzbericht, und
der kostet zur Laufzeit nichts – er greift erst, wenn schon etwas kaputt ist.

**Was bleibt:** Der Fehler tritt **sporadisch** auf. Über seine Häufigkeit ist
nichts bekannt, über einen Auslöser ebenfalls nichts. Die Beobachtungen bisher:
zwei Abstürze beim Start an *verschiedenen* Stellen, einer beim Beenden,
dazwischen mehrere vollständige Läufe.

*Regel, gegen die ich selbst verstoßen habe:* Aus zwei Läufen wird keine
Ursache. Bei einem sporadischen Fehler ist die naheliegende Erklärung meist
die, die man zuletzt geändert hat – und genau deshalb ist sie verdächtig, nicht
belegt. Wer sie festschreibt, sucht danach an der falschen Stelle weiter.

**Konsequenz für das Vorgehen:** Statt weiterer Marken – die das Zeitverhalten
verändern und deren Wirkung, wie sich zeigt, nicht einmal reproduzierbar ist –
liegt jetzt ein Ausnahmebehandler im Programm
(`embedder/src/platform/crash_handler_horizon.cpp`). Er kostet nichts, bis es
kracht, und liefert dann PC, LR, SP, die angefasste Adresse und einen
Bezugspunkt zum Auflösen. Der nächste Absturz sagt uns, wo er sitzt, statt uns
raten zu lassen.

---

## 2026-08-10 – Die Instrumentierung hielt den Start zusammen

> **Überholt, siehe Eintrag darüber.** Die hier gezogene Schlussfolgerung hat
> sich nicht bestätigt. Der Befund selbst – zwei Läufe, unterschiedliches
> Verhalten – bleibt richtig, die Erklärung nicht.

**Befund:** Nach dem Ausbau der Debug-Marken stürzte der Start ab. Derselbe
Stand mit `TRACE=1` neu gebaut läuft durch – `kSuccess`, alle sechs Schriften,
Hauptschleife. Zwei Bauten, ein Quellstand, unterschiedliches Verhalten:

| Bau | Verhalten |
|---|---|
| mit Marken | läuft durch |
| ohne Marken | Absturz nach dem Konstruktor von `EventHandlerImplementation` |

Jede Marke schreibt über TCP. Das kostet Millisekunden an genau den Stellen,
an denen Threads starten. **Es ist also ein Rennen, kein Logikfehler** – und
die Marken haben es die ganze Zeit zugedeckt.

Dazu passt, was vorher unerklärlich schien: Zwei frühere Abstürze lagen an
*verschiedenen* Stellen, und ein Lauf dazwischen war sauber. Das war kein
Messfehler und kein unvollständiger Upload, wie zwischenzeitlich vermutet,
sondern dasselbe Rennen mit anderem Ausgang.

**Verdacht** (`bin/eventhandler.cc:22-36`):

```cpp
event_handler = new EventHandler();
event_handler->delegate_.Start(event_handler);   // Poll-Thread startet
if (!SocketBase::Initialize()) { ... }           // erst danach
```

Der Poll-Thread läuft sofort los und ruft `poll()` auf dem
Loopback-Weckkanal, während der Hauptthread noch bei `SocketBase::Initialize()`
ist. Unter Linux fällt das nicht auf, weil dort praktisch nichts passiert; auf
Horizon hängt der gesamte Socket-Stack an einem Systemdienst.

Zu prüfen wäre die Reihenfolge – `SocketBase::Initialize()` vor den
Thread-Start. **Wichtig für den Test: Er ist nur mit ausgeschalteten Marken
aussagekräftig**, weil der Fehler sonst nicht auftritt.

**Nachtrag – es betrifft auch das Beenden.** Im selben Lauf (mit Marken, Start
sauber) stürzte die Konsole beim Plus-Druck ab. Dieselbe Stelle war heute
Abend schon einmal in Ordnung, mit `shutdown_dart_vm_when_done = true` und
ohne die Änderungen am Instrumentierungsumfang. Der Abbau geht denselben Weg
wie der Aufbau, nur rückwärts: `Dart_Cleanup` → `EventHandler::Stop()` →
Weckmeldung über den Loopback-Kanal → der Poll-Thread beendet sich und meldet
`NotifyShutdownDone`, worauf der Hauptthread wartet.

Damit steht der Verdacht nicht mehr nur auf der Reihenfolge in
`EventHandler::Start`, sondern auf dem **Lebenszyklus des Poll-Threads und
seines Weckkanals** insgesamt. Beide Enden über einen einzigen Socket zu
synchronisieren, dessen Dienst selbst auf- und abgebaut wird, ist die Stelle,
an der beides zusammenläuft.

Die Fassung ohne Weckdeskriptor, die als Rückfall in
`eventhandler_horizon.cc` steht und bisher nie zum Zug kam, wäre hier
möglicherweise die robustere Wahl: Sie kommt ohne Socket aus und pollt mit
einer Zeitschranke. Das ist zu prüfen, nicht anzunehmen – aber es ist der
naheliegende Gegenentwurf, falls sich der Kanal als Ursache bestätigt.

**Was daran über das Vorgehen zu lernen ist:** Instrumentierung verändert das
Verhalten, das sie beobachten soll. Heute hat sie erst einen Fehler sichtbar
gemacht (drei stumme Kanäle) und dann einen anderen verdeckt. Ein Baum gilt
deshalb erst als geprüft, wenn er **ohne** Marken gelaufen ist – der Ausbau ist
keine Kosmetik, sondern der eigentliche Test.

---

## 2026-08-10 – Systemschriften: gemessen statt geglaubt

**Befund:** Auf Hardware erschienen die Rechtecke, aber kein Text – genau die
Aufteilung, für die die Textzeile in `examples/ui_app/dart/main.dart` gedacht
war. Ursache: `txt/BUILD.gn` wählt die Plattformdatei über eine `is_*`-Kette,
und Horizon fiel in den `else`-Zweig auf `txt/src/txt/platform.cc:19` –
`SkFontMgr_New_Custom_Empty()`, ein Manager ohne eine einzige Schrift.
FreeType war gebaut, `SkTypeface_FreeType` gelinkt, der Textpfad vollständig.
Es gab nur nichts zu setzen.

**Konsequenz:** Die Konsole bringt eigene Schriften mit, erreichbar über den
Dienst `pl:u` (`plGetSharedFontByType`). Dieselbe Aufteilung wie bei den
Stackgrenzen und der Logsenke:

- `embedder/src/platform/fonts_horizon.cpp` öffnet den Dienst und reicht die
  sechs Schnitte durch – Standard zuerst, damit sie Vorgabefamilie wird, dann
  die CJK-Schnitte, zuletzt die Nintendo-Sonderzeichen. Ohne Kopie: Der
  Speicher gehört dem System und bleibt gültig.
- `txt/src/txt/platform_horizon.cc` baut daraus `SkFontMgr_New_Custom_Data`,
  ohne je einen libnx-Header zu sehen.
- `skia/BUILD.gn` braucht dafür `skia_enable_fontmgr_custom_embedded = true` –
  ohne das existiert `SkFontMgr_New_Custom_Data` nicht einmal als Symbol.

**Der eigentliche Befund betrifft das Vorgehen.** Ich war überzeugt, dass
Nintendo die Schriften im **BFTTF**-Format liefert, also nicht als rohes TTF,
und hätte fast eine Umwandlung eingebaut. Statt darauf zu bauen, protokolliert
die Embedder-Seite Größe und die ersten acht Bytes jeder Schrift. Auf Hardware:

```text
Schrift 1: 7848200 Bytes, Kopf 00 01 00 00 00 10 01 00
Schrift 2:  123912 Bytes, Kopf 00 01 00 00 00 0f 00 80
Schrift 5:  180236 Bytes, Kopf 00 01 00 00 00 0d 00 80
```

`00 01 00 00` ist die TrueType-Signatur. **Die Daten sind rohes TTF**, eine
Umwandlung hätte sie zerstört. Eine verbreitete Auskunft ist kein Beleg, und
acht Bytes im Protokoll kosten weniger als ein halber Zyklus.

**Nebenbefund:** In `skia/BUILD.gn` standen zwei `is_horizon`-Blöcke – anders
als bei den GN-Duplikaten von vorhin *nicht* identisch, sondern eine ältere und
eine erweiterte Fassung. GN wertet beide aus, die spätere gewinnt; der erste
war toter Code, der beim Lesen in die Irre führt. Solche Reste sind
gefährlicher als exakte Kopien, weil sie plausibel aussehen.

---

## 2026-08-10 – `leak_vm` ist auf einer Konsole keine harmlose Voreinstellung

**Befund:** Die Konsole stürzte beim Beenden ab, obwohl `main()` bis zur
letzten Zeile durchlief – `Shutdown ...` und `ui_app beendet` standen beide im
Protokoll. Der Fehler lag also in der Abbauphase nach `return 0`.

Ein Messlauf hat die naheliegende Erklärung ausgeschlossen: drei Sekunden
Wartezeit nach `FlutterEngineShutdown`, mit offener Logsenke. **Keine einzige
Meldung** – kein VM-Thread allokierte noch, und das Freigeben des Framebuffers
war ebenfalls unschädlich.

Damit rückte ein Thread in den Blick, der *wartet* statt zu arbeiten:

```cpp
// common/settings.h:278
bool leak_vm = true;
// embedder.cc:2079
settings.leak_vm = !SAFE_ACCESS(args, shutdown_dart_vm_when_done, false);
```

`FlutterEngineShutdown` baut die Shell ab, lässt die Dart-VM aber stehen –
gedacht als Warmstart für Embedder, die die Engine mehrfach starten. Damit
läuft `Dart_Cleanup` nie, also auch nicht `EventHandler::Stop()`, und dessen
Poll-Thread hängt weiter in `poll()` auf dem Loopback-Weckkanal. Beim
geordneten Abbau der NRO verschwindet der Socket-Dienst unter ihm weg.

**Konsequenz:** `project.shutdown_dart_vm_when_done = true`. Auf Hardware
bestätigt: sauberer Rücksprung ins hbmenu.

**Was daran verallgemeinerbar ist:** Auf Linux, Android und iOS ist `leak_vm`
folgenlos, weil `exit()` den Prozess beendet und das System aufräumt. Eine
Homebrew-NRO wird dagegen geordnet abgebaut. Voreinstellungen, die anderswo
nur Aufräumarbeit sparen, können hier zum Fehler werden – dieselbe Klasse wie
`GetCachesDirectory`, nur mit umgekehrtem Vorzeichen.

**Zum Vorgehen:** Der Messlauf hat nichts repariert und war trotzdem der
entscheidende Schritt – er hat die Hypothese „weiterlaufende VM-Threads
allokieren" widerlegt und damit den Blick auf einen *wartenden* Thread
gelenkt. Dass die Logsenke dabei bis zuletzt offen blieb, war Absicht: Vorher
wurde sie geschlossen, bevor der Abbau begann, und hätte jede Meldung aus
genau der Phase verschluckt, in der es kracht.

---

## 2026-08-10 – Der Snapshot muss aus demselben Baum kommen wie die VM

**Befund:** Mit angeschlossenem `FML_LOG` kam der Fehler im Klartext:

```text
Wrong full snapshot version, expected '0150713ccc165a92bb03706c55150060'
                            found    '78da37fed6bf1489361a312568249f3f'
```

`dart/tools/make_version.py:20-45` bildet `Version::SnapshotString()` als MD5
über **15 Quelldateien** – unter anderem `dart.cc`, `app_snapshot.cc`,
`image_snapshot.cc`, `object.h`, `raw_object.h`, `snapshot.h`. Die Portierung
fasst mehrere davon an; schon der Horizon-Zweig in `Dart::VersionString`
genügt. Damit ist jeder fremd erzeugte Snapshot ungültig.

Der zweite Grund wiegt schwerer und gilt unabhängig vom Hash: Wir bauen mit
`dart_use_compressed_pointers=false`, der `gen_snapshot` aus den
Android-arm64-Artefakten erzeugt Snapshots **mit** Compressed Pointers. Selbst
bei passendem Hash hätte die Merkmalsprüfung ihn zu Recht abgelehnt.

**Damit ist der Shortcut aus Meilenstein 1b erledigt.** „gen_snapshot muss nicht
selbst gebaut werden" galt für den *Ladeweg* – Assembly erzeugen, assemblieren,
linken, Magic prüfen. Für die *Ausführung* gilt es nicht. Der Unterschied war
in Meilenstein 1b ausdrücklich vermerkt („Was das nicht zeigt: dass der
Snapshot läuft"), aber die Konsequenz für gen_snapshot war nicht gezogen.

**Konsequenz:** `clang_x64/gen_snapshot_product` aus demselben Checkout, mit
denselben `args.gn`. Das Ziel existierte bereits in der Konfiguration, Clang
liegt in `flutter/buildtools`. Zwei Lücken standen im Weg, beide in Code, den
die AOT-Runtime nie übersetzt – sie *liest* Snapshots, sie schreibt keine:

- `vm/compiler/ffi/abi.cc:65` – `#error Unknown OS`. Der Mechanismus setzt aus
  OS- und Architekturnamen einen Enum-Wert `k<OS><Arch>` zusammen. Ein eigener
  Wert `kHorizonArm64` müsste durch Kernel, Snapshots und alle
  Vergleichsstellen gereicht werden und würde nichts unterscheiden: Horizon
  folgt auf arm64 derselben Aufrufkonvention wie Linux (AAPCS64). Also die
  Linux-ABI benutzen statt eine eigene erfinden.
- `vm/image_snapshot.cc` – **acht** Weichen über die Form der Assembly-Ausgabe,
  `UNIMPLEMENTED()` für Unbekanntes. Horizon gehört zur ELF-Gruppe mit
  `.size`/`.type`; dass devkitA64 genau diese Direktiven übersetzt, war in
  Meilenstein 1b schon belegt.

Ein dritter Fehler kam beim Linken heraus: `virtual_memory_horizon.cc` war die
einzige der fünf Horizon-Quellen **ohne** `#if defined(DART_HOST_OS_HORIZON)`.
Beim Ziel-Build fällt das nie auf, weil dort `virtual_memory_posix.cc` durch
seinen eigenen Wächter verschwindet. Beim Host-Build ist der Host Linux – beide
Dateien aktiv, zehn doppelte Symbole.

*Muster:* Ein fehlender Wächter ist auf der Zielplattform unsichtbar. Erst der
Host-Build deckt ihn auf. Wer eine Plattformdatei anlegt, sollte den Wächter
nicht als Formsache behandeln.

**Arbeitsregel:** Snapshot und Engine kommen immer aus demselben Stand. Wer
eine der 15 Hash-Dateien anfasst – auch nur, um eine Debug-Marke zu entfernen –,
muss `gen_snapshot` **und** Engine neu bauen. Sonst ist der nächste Lauf ein
Versionskonflikt mit anderen Zahlen.

**Ergebnis:** `… arm64 horizon no-compressed-pointers` im Merkmalsstring des
Snapshots, `FlutterEngineRunInitialized = kSuccess`, und der blaue Balken aus
`examples/ui_app/dart/main.dart` läuft über den Bildschirm der Konsole.

---

## 2026-08-10 – Drei stumme Kanäle, drei Fehldiagnosen

**Befund:** Der Absturz in `FlutterEngineRunInitialized` sah dreimal
hintereinander wie ein harter Speicherzugriffsfehler aus. Dreimal war es ein
sauber erkannter Fehler, dessen Meldung nur nirgendwo ankam:

| Meldeweg | landet bei | angeschlossen |
|---|---|---|
| `OS::Print` / `OS::PrintErr` | TCP-Senke | war schon da |
| `Syslog::PrintErr` – **hier meldet `FATAL`** (`platform/assert.cc:37`) | `stderr` | fehlte |
| `FML_LOG` – **hier meldet die Engine** (`fml/logging.cc`) | `stderr` | fehlte |

Auf der Konsole geht `stderr` nirgendwohin. Beide Kanäle enden mit `abort()`
(`Assert::Fail` bzw. `KillProcess()`), und `abort()` erzeugt den Fatal-Screen
`2162-0002` – ununterscheidbar von einem echten Data Abort.

**Die falsche Schlussfolgerung stand im Protokoll:** „Die VM stirbt, bevor sie
etwas zu melden hat." Aus einer ausbleibenden Meldung darf man das nur
schließen, wenn belegt ist, dass der Kanal trägt. Belegt war nur, dass *ein*
Kanal trägt – und die aufschlussreichsten Meldungen liefen über die anderen
beiden.

*Regel:* Ein Diagnosekanal ist erst dann einer, wenn jede Klasse von Meldungen
darin ankommt. Dieselbe Lehre wie beim Zähler in `relink-example.sh`, der nur
`undefined reference` kannte und bei jeder anderen Fehlerart Erfolg meldete.

**Nachtrag zum selben Muster:** Auch `relink-example.sh` sah noch weg – es
zählte Linkerfehler über einem Protokoll, in dem `make` schon an einem
Compilerfehler gescheitert war, und meldete „0 undefinierte Symbole", während
die alte NRO unverändert liegen blieb. Prüft jetzt den Exit-Code.

---

## 2026-08-10 – `socketpair` ist ein Attrappensymbol, Loopback trägt

**Befund:** Der erste Lauf mit angeschlossener Syslog-Senke lieferte sofort eine
verwertbare Meldung – die erste, die je aus der Dart-VM auf der Konsole
herauskam:

```text
FlutterEngineInitialize = 0 (kSuccess)
FlutterEngineRunInitialized ...
runtime/bin/eventhandler_horizon.cc: 55: error:
    Failed creating socketpair for event handler: 88
```

`88` ist `ENOSYS`. Die Ursache steht wörtlich in libnx
(`nx/source/runtime/devices/socket.c:812`):

```c
int socketpair(int domain, int type, int protocol, int sv[2]) {
    // Unimplementable, function definition written for compliance
    errno = ENOSYS;
    return -1;
}
```

**Woher der Fehlschluss kam.** Die Entscheidung „`socketpair()` statt Pipe"
stützte sich auf eine `nm`-Tabelle über `libnx.a`: `pipe` fehlt, `socketpair`
ist definiert. Definiert heißt hier aber nur *vorhanden, damit Code linkt*.
Beim Nebenbefund zu den Semaphoren war dieselbe Frage richtig beantwortet
worden – dort wurde zusätzlich das Disassemblat angesehen und festgestellt, dass
`sem_init` tatsächlich Argumente prüft und einen Zähler ablegt. Bei
`socketpair` blieb es bei der Symbolliste.

*Regel:* Ein Symbol in einer Bibliothek belegt Linkbarkeit, nicht Funktion.
Wo eine Plattformentscheidung daran hängt, muss der Rumpf angesehen werden.

**Konsequenz:** Gebraucht wird nur ein Deskriptor, auf den `poll` warten kann
und den ein anderer Thread beschreibt. Zwei Fassungen, und welche greift,
entscheidet die Hardware statt einer Vorabannahme:

1. Ein selbst geknüpftes TCP-Paar über `127.0.0.1` – `bind` auf Port 0,
   `listen`, `connect`, `accept`.
2. Andernfalls gar kein Weckdeskriptor: Weckmeldungen in ein
   mutex-geschütztes Fach, und `poll` bekommt eine Schranke von 20 ms.

Der Lauf auf Hardware hat entschieden: **`EventHandler: Weckkanal ueber
127.0.0.1`** – libnx' bsdsocket-Dienst stellt Loopback zu, Weg 1 trägt. Weg 2
bleibt als Rückfall stehen und kostet nichts.

Für die Zerlegung von `HandleInterruptFd` in `ProcessInterruptMessage` gab es
dabei einen zweiten Grund neben dem Fach: Dieselbe Nachrichtenbehandlung wird
jetzt von zwei Quellen aus gebraucht, und eine Kopie hätte beim nächsten
Upstream-Abgleich auseinanderlaufen können.

**Nebenbefund:** `pipe()` in `posix_compat_horizon.cpp` stand auf demselben
toten `socketpair` – bisher unbemerkt, weil es niemand gerufen hat. Das
Verhalten (`-1` mit `ENOSYS`) war zufällig richtig, der Kommentar daneben
falsch.

**Was danach kam:** Der Lauf endet unmittelbar nach der Loopback-Zeile, wieder
ohne Meldung. Da ein `FATAL` jetzt sichtbar wäre, ist es erneut ein harter
Speicherzugriffsfehler. Übrig bleiben zwei Schritte in `EventHandler::Start`
(`bin/eventhandler.cc:22-36`): der Start des Poll-Threads und
`SocketBase::Initialize`. Nicht die Ursache ist die Stackgröße –
`bin::Thread::GetMaxStackSize()` liefert 1 MB und damit die von libnx
verlangte 4K-Ausrichtung.

---

## 2026-08-10 – Der stumme Absturz: zwei Fehler, einer davon im Meldeweg

**Befund 1 – `GetCurrentStackBounds` = `false` ist ein Abbruch, kein Rückfall.**
Der Vermerk in `os_thread_horizon.cc` („Die VM wertet false als unbekannt aus …
fällt auf konservativere Verfahren zurück") stimmt für die gepinnte Dart-Fassung
nicht. `vm/os_thread.cc:49-52`:

```cpp
  // Try to get accurate stack bounds from pthreads, etc.
  if (!GetCurrentStackBounds(&stack_limit_, &stack_base_)) {
    FATAL("Failed to retrieve stack bounds");
  }
```

Der Aufruf steht im Konstruktor **jedes** `OSThread`. Der erste entsteht in
`OSThread::Init()` (`vm/dart.cc:374`), also innerhalb von `Dart_Initialize`.

**Befund 2 – die VM startet erst in `RunInitialized`, nicht in `Initialize`.**
Das war die Stelle, an der die bisherige Eingrenzung zu kippen drohte: Wenn
`FlutterEngineInitialize` `kSuccess` meldet, kann darin kein FATAL gefallen
sein. Die Kette zeigt, dass beides zusammenpasst –
`FlutterEngineInitialize` baut nur Settings, Callbacks und die
`RunConfiguration`; `FlutterEngineRunInitialized` beginnt mit
`EmbedderEngine::LaunchShell()` (`embedder.cc:2476`), und erst das führt über
`Shell::Create` → `InferVmInitDataFromSettings` → `DartVMRef::Create` →
`Dart_Initialize` (`runtime/dart_vm.cc:441`) zur VM. Der FATAL ist damit das
Erste, was in `RunInitialized` überhaupt passieren kann – genau dort, wo der
Absturz gemessen wurde, und vor jeder Heap-Anforderung.

**Befund 3 – der Meldeweg der VM war nie angeschlossen.** `FATAL` läuft über
`Assert::Fail` → `DynamicAssertionHelper::Print` → **`Syslog::PrintErr`**
(`platform/assert.cc:37`) – nicht über `OS::PrintErr`, an dem die TCP-Senke
hängt. `syslog_linux.cc`, dessen Wächter für Horizon erweitert wurde, schreibt
mit `vfprintf(stderr, …)`; auf der Konsole geht `stderr` nirgendwohin.

Das entkräftet die bisherige Schlussfolgerung „die VM stirbt, bevor sie etwas zu
melden hat". Sie hatte etwas zu melden – es gab nur keinen Kanal. Ein
Diagnosekanal, der die halbe Klasse von Meldungen nicht führt, ist derselbe
Fehler wie der Zähler in `relink-example.sh`, der nur eine Fehlerart kannte.

**Konsequenz 1 – Syslog auf dieselbe Senke.** `Syslog::VPrint`/`VPrintErr`
bedienen für Horizon zusätzlich `flutter_libnx_vm_log`, die schwach gebundene
Funktion des Embedders. Fehlt der Embedder, ist der Zeiger null und es bleibt
beim bisherigen Verhalten.

**Konsequenz 2 – echte Stackgrenzen.** Der Kernel weiß sie, auch ohne
`pthread_getattr_np`: `svcQueryMemory` liefert zu einer Adresse die umgebende
Speicherregion, und für den aktuellen Stackpointer ist das der Stack. Das gilt
für beide Thread-Arten:

- Nebenläufige Threads laufen auf einem `stack_mirror`, den libnx per
  `svcMapMemory` einblendet (`nx/source/kernel/thread.c:132`). Die Abbildung
  umfasst genau Stack, TLS und reent-Struktur.
- Der Hauptthread bekommt seinen Stack vom Loader, ebenfalls als eigene Region.

Der Aufruf liegt in `embedder/src/platform/stack_bounds_horizon.cpp`, weil
`<switch.h>` in der Dart-VM Bezeichner kollidieren lässt – dieselbe Trennung wie
bei `random_horizon.cpp` und der Logsenke. Plausibilität wird geprüft: Der
Stackpointer muss in der Region liegen und die Region darf nicht größer als
64 MB sein, sonst wurde etwas anderes getroffen (etwa der ganze Heap) und die
Funktion sagt ehrlich `false`.

Der Notnagel in `os_thread_horizon.cc` rät dann bewusst in die sichere Richtung –
256 KB unterhalb des aktuellen Stackpointers. Eine zu hoch angesetzte
Untergrenze meldet einen Stapelüberlauf zu früh, als kontrollierte
Dart-Ausnahme; eine zu niedrige übersieht ihn und endet in einem echten
Speicherzugriffsfehler.

**Was der Fatal-Screen nicht erklärt:** `Assert::Fail` ruft vor `abort()` noch
`Dart_DumpNativeStackTrace` und `Dart_PrepareToAbort`. Ersteres ist im
Product-Modus leer (`dart_api_impl.cc:7262-7266`, der Rumpf hängt an
`DART_INCLUDE_PROFILER`), Letzteres reicht an `OS::PrepareToAbort` durch. Ein
sauberes `abort()` sollte also kein Data Abort sein. Ob `2168-0002` vom
`abort()`-Pfad kommt oder von etwas dahinter, sagt erst der nächste Lauf – jetzt
mit sichtbarer VM-Meldung.

---

## 2026-08-10 – Von 100 auf 30 offene Symbole: dart:io steht

**Befund:** Die Gruppen aus der Landkarte ließen sich fast alle so abarbeiten, wie sie
sortiert waren. Was dabei über den Zuschnitt der Arbeit klar wurde:

*Guards statt Neuschreiben.* `socket_base_posix.cc` (483 Zeilen, 29 Symbole),
`console_posix.cc`, `namespace_linux.cc` und `security_context_linux.cc` brauchten nur
`DART_HOST_OS_HORIZON` im `#if`. Drei Nebenwirkungen mussten mit:

- `console_posix.cc` bindet `<termios.h>` ein, das bei newlib auf ein nicht vorhandenes
  `<sys/termios.h>` verweist. Beide Funktionen der Datei sind leer – der Include stammt
  aus einer Zeit, als sie noch etwas taten.
- `socket_base_posix.cc` holt `RawAddr` über `socket_base_macos.h`, das `<sys/un.h>`
  braucht. Für Horizon zeigt der Include jetzt auf `socket_base_horizon.h`.
- `bin/ifaddrs.h` suchte den Systemheader. Upstream hat dafür längst einen Zweig – er
  entstand für Android vor API 24 und erklärt `struct ifaddrs` selbst. Wieder ein Fall,
  in dem die Vorkehrung schon da war.

*Kurze ehrliche Fassungen statt Portierung.* `process_horizon.cc` ersetzt 1203 Zeilen
durch 140. Auf Horizon gibt es keine Kindprozesse, also ist die richtige Antwort auf
`Process::Start` ein sichtbarer Fehlschlag mit `ENOSYS` – nicht der Versuch, etwas
nachzubauen. Was auch ohne Prozesse gebraucht wird (globaler Exitcode, Exit-Hook),
funktioniert vollständig. `file_system_watcher_horizon.cc` meldet `IsSupported() = false`;
dart:io hat diese Frage genau für solche Plattformen vorgesehen, die Dart-Seite wirft
dann eine aussagekräftige Ausnahme, statt auf Ereignisse zu warten, die nie kommen.

*Zufall.* `crypto_linux.cc` liest `/dev/urandom`, ersatzweise `SYS_getrandom` – beides
gibt es nicht. newlib bringt aber `getentropy()` mit und reicht an `_getentropy_r`
weiter, das die Plattform zu stellen hat. `embedder/src/platform/random_horizon.cpp`
liefert es über `csrngGetRandomBytes`, den kryptographisch geeigneten Generator des
Systems. Das bedient zugleich BoringSSL, das dieselbe Funktion zieht. Die Datei steht
allein, damit `<switch.h>` und seine Makros nicht in fremden Übersetzungseinheiten
landen – dieselbe Vorsicht wie bei `os_horizon.cc`.

*POSIX-Lücken.* In die Compat-Schicht kamen `fstatat`, `fchdir`, `open64`, `openat64`,
`pread`, `pipe`, `pthread_sigmask`, `readlinkat`, `symlinkat`, `utimensat`. Drei
Entscheidungen darin sind erwähnenswert:

- `pread` gibt es nicht, also bleibt suchen-lesen-zurücksetzen. Das ist nicht atomar,
  deshalb eine Sperre; ohne sie würden zwei Threads am selben Deskriptor einander die
  Position verstellen.
- `pipe` läuft über `socketpair`, wie schon der Eventhandler.
- `pthread_sigmask` tut nichts und meldet Erfolg. Das ist kein verschwiegener Fehler:
  Horizon stellt keine Signale zu, „alles blockiert" und „nichts blockiert" sind
  dasselbe.
- `readlinkat` gibt `EINVAL` – genau das, was POSIX für „ist kein Verweis" vorsieht. Das
  Dateisystem der Switch kennt keine symbolischen Verweise.

**Ergebnis:** 100 → 30 offene Symbole. Übrig bleiben drei Gruppen, alle mit
Build-Ursachen statt Portierungsursachen.

---

## 2026-08-10 – Zwei Fehler, die sich gegenseitig verdeckt haben

**Befund 1 – Geschwister mitgenommen.** `process_horizon.cc` bekam
`Process::ClearAllSignalHandlers()` dazu, weil sie neben `ClearSignalHandler` und
`ClearSignalHandlerByFd` ins Bild passte. Sie stand aber nicht in der Liste der
vermissten Symbole – aus gutem Grund: `process.cc:71` definiert sie plattformneutral.
Ergebnis war ein Linkfehler wegen doppelter Definition.

*Regel:* Maßgeblich ist, welche Symbole der Linker wirklich vermisst, nicht welche
Geschwister eine Datei vollständig aussehen lassen. Dieselbe Falle wie früher bei
`GetCurrentStackPointer()` in `os_thread_horizon.cc`.

**Befund 2 – ein Zähler, der wegsieht.** `relink-example.sh` zählte nur
`undefined reference` und meldete **„0 undefinierte Symbole"**, während der Link an
ebendieser doppelten Definition scheiterte. Dazu kam, dass das Protokoll unter `/tmp`
lag und zwischen zwei WSL-Aufrufen verschwand – ein leeres Protokoll sieht bei einer
reinen Zählung genauso aus wie ein fehlerfreies.

Aufgefallen ist es nur, weil die anschließende Prüfung die ELF nicht fand. Beinahe wäre
ein Erfolg gemeldet worden, den es nicht gab.

*Konsequenz:* Das Skript prüft jetzt dreierlei getrennt – undefinierte Symbole, doppelte
Definitionen, sonstige Linkerfehler – und bricht ab, wenn das Protokoll leer ist. Es
schreibt außerdem ins Repository statt nach `/tmp`. Ein Prüfwerkzeug, das nur eine
Fehlerart kennt, meldet bei jeder anderen Erfolg.

---

## 2026-08-10 – Drei Build-Ursachen: FreeType, Zertifikate, abseil

**Befund 1 – FreeType.** Die Bibliothek selbst wurde gebaut (24 Objektdateien), Skias
Anbindung daran aber nicht: null Objekte für `skia_ports_freetype_sources`. Grund ist der
`is_horizon`-Block in `flutter/skia/BUILD.gn`, der `skia_use_freetype` nicht setzt – der
Vorgabewert hängt an Plattformen, die Horizon nicht kennt. Der Fuchsia-Block direkt
darüber setzt es aus demselben Grund ausdrücklich. Ohne Typeface gäbe es keinen Text.

**Befund 2 – Wurzelzertifikate.** `root_certificates_pem_length` fehlte. Der Baum kennt
`root_certificates_unsupported.cc` und ein Ziel `fallback_root_certificates`, gesteuert
über `dart_use_fallback_root_certificates`. Auf Horizon ist das keine Notlösung, sondern
der einzige Weg: Es gibt keinen Systemzertifikatspeicher und kein `/etc/ssl`. Ohne das
Flag wäre jede HTTPS-Verbindung unmöglich – und damit Referenz-App.

**Befund 3 – abseil.** `low_level_alloc.o` wird gebaut, enthält aber **null Symbole**.
`low_level_alloc.h:39-41` setzt `ABSL_LOW_LEVEL_ALLOC_MISSING`, sobald `ABSL_HAVE_MMAP`
fehlt – für Horizon zutreffend.

Die Folge ist größer, als sie aussieht: `create_thread_identity.cc` ist **vollständig**
in `#ifndef ABSL_LOW_LEVEL_ALLOC_MISSING` gehüllt und trägt in Zeile 19 den Satz *„This
file is a no-op if the required LowLevelAlloc support is missing."* Ohne
`CreateThreadIdentity` gibt es kein `absl::Mutex`. Das ist keine Panne, sondern abseils
dokumentierte Haltung: Plattformen ohne mmap sollen `absl::Mutex` nicht benutzen.

Verursacher ist **re2** (`re2/dfa.cc`, `re2/regexp.cc`), das
`//third_party/abseil-cpp/absl/synchronization` als Abhängigkeit führt.

**Konsequenz:** Der Ausweg steht schon in der Datei. Neben dem POSIX-Weg gibt es einen
zweiten über `VirtualAlloc`/`VirtualFree` für Windows – ein dritter nach demselben Muster
ist also vorgesehene Bauart, kein Fremdkörper. Was die Arena wirklich braucht, ist
seitenweise ausgerichteter Speicher, nicht eine eigene Abbildung; das leistet `memalign`.
Dieselbe Entscheidung wie bei Darts `virtual_memory_horizon.cc` und Skias `sk_fdmmap`.

Bewusst wird `ABSL_HAVE_MMAP` **nicht** gesetzt: Das wäre gelogen und würde an anderen
Stellen zu echten mmap-Aufrufen führen. Entkräftet wird nur die eine Folgerung, dass ohne
mmap auch kein LowLevelAlloc möglich sei.

Nicht gesetzt wird ebenfalls `ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING` – sonst
verschwände `SigSafeArena()`, das `borrowed_fixup_buffer.cc` braucht. Die Frage der
Signalsicherheit ist auf Horizon ohnehin gegenstandslos, weil keine Signale zugestellt
werden. Dieselbe Überlegung wie bei `pthread_sigmask`.

---

## 2026-08-10 – Die Engine initialisiert auf echter Switch-Hardware

**Befund:** `engine_link_test.nro` (7,4 MB) per Netloader auf die Switch übertragen und
gestartet. Protokoll über die TCP-Senke:

```
FlutterEngineRunsAOTCompiledDartCode = 1
sizeof(FlutterProjectArgs) = 312
FlutterEngineGetCurrentTime = 1672601270128108864
FlutterEngineInitialize wird aufgerufen
FlutterEngineInitialize = 0 (kSuccess)
Engine initialisiert und wieder heruntergefahren
```

**Konsequenz:** Damit ist mehr belegt als „es linkt". `FlutterEngineInitialize` erzeugt
die Shell samt ihrer Threads; wäre `MessageLoopImpl::Create()` noch bei `nullptr`
geblieben, wäre genau hier Schluss gewesen. Auch `FlutterEngineShutdown` läuft sauber
durch.

Was damit **nicht** belegt ist: dass Dart-Code läuft. Die Root-Isolate startet erst bei
`FlutterEngineRunInitialized`.

---

## 2026-08-10 – dart:io ist zweistufig, und die untere Stufe war übersehen

**Befund:** Nach dem Einbau von `FlutterEngineRunInitialized` blieben **100** Symbole
offen. Auffällig: `socket_base_linux.cc` trug bereits einen Horizon-Guard und übersetzte
zu 192 Symbolen – trotzdem fehlten 29 `SocketBase::`-Funktionen.

Der Grund ist der Aufbau von dart:io. `*_posix.cc` trägt die gemeinsame
Unix-Implementierung, `*_linux.cc` nur das, was sich zwischen den Unixen unterscheidet.
`socket_base_linux.cc` definiert `JoinMulticast`, `LeaveMulticast`, `LookupAddress`,
`SetMulticastLoop` und `GetType` – `Read`, `Write`, `Close`, `GetPort` und der ganze Rest
liegen in **`socket_base_posix.cc`**, deren Guard Horizon nicht kannte.

**Konsequenz:** Guards müssen auf beiden Ebenen erweitert werden. Die POSIX-Ebene ist
klein – nur drei Dateien in `runtime/bin` (`console_posix.cc`, `socket_base_posix.cc`,
`virtual_memory_posix.cc`), von denen die letzte gar nicht referenziert wird.

**Landkarte der 100 Symbole** (`scripts/group-missing-symbols.sh`,
`scripts/analyze-dart-io-posix.py`):

| Gruppe | Anzahl | Weg |
|---|---|---|
| `SocketBase` | 29 | `socket_base_posix.cc`: Guard erweitern; nur `ListInterfaces` braucht `getifaddrs` |
| `Skia FreeType` | 22 | `SkTypeface_FreeType` fehlt trotz `skia_use_freetype = true` – Bibliothek wird nicht mitgelinkt |
| `Process` | 14 | eigene Fassung: kein `fork`, kein `exec`, keine Signale |
| `FileSystemWatcher` | 8 | eigene Fassung: kein inotify, `IsSupported()` = false |
| `Namespace` | 7 | Guard erweitern – die `*at`-Funktionen gibt es jetzt |
| POSIX-Lücken | 8 | `fstatat`, `readlinkat`, `symlinkat`, `utimensat`, `pread`, `pipe`, `pthread_sigmask`, `_getentropy_r` |
| abseil | 5 | `LowLevelAlloc`, `SigSafeArena`, Thread-Identity |
| Crypto/SSL | 4 | `getentropy` lässt sich über libnx' `csrngGetRandomBytes` bedienen |

Wichtig an der Verteilung: Der größte Block (Sockets) ist reine Guard-Arbeit, kein
Neuschreiben. Von den 1203 Zeilen in `process_linux.cc` muss dagegen nichts übernommen
werden – auf Horizon gibt es keine Kindprozesse, also ist die ehrliche Fassung eine
kurze.

---

## 2026-08-10 – Eine Plattformweiche, die still nichts liefert

**Befund:** `fml/message_loop_impl.cc:29-43` wählt die Nachrichtenschleife über eine
`#if`-Kette. Für Horizon greift keiner der Zweige, also der `#else`: **`return nullptr`**.
Das übersetzt und linkt anstandslos und fällt erst zur Laufzeit auf – und zwar sofort,
denn die Shell richtet für UI-, Raster- und IO-Thread je eine Schleife ein.

Die Linux-Fassung (`platform/linux/message_loop_linux.cc`) baut auf `epoll` und
`timerfd` auf; beides fehlt auf Horizon. Im gelinkten Programm war keines der Symbole
`timerfd_create`, `epoll_create1`, `epoll_wait`, `eventfd` auch nur referenziert – ein
sauberer Beleg dafür, dass tatsächlich *keine* Schleife gebaut wurde.

**Konsequenz:** `platform/horizon/message_loop_horizon.{h,cc}` auf Basis von
`std::condition_variable`. Die Schnittstelle verlangt nur dreierlei – `Run`, `Terminate`,
`WakeUp(TimePoint)` –, und eine Bedingungsvariable leistet genau das: bis zu einem
Zeitpunkt warten und vorzeitig geweckt werden können. Der Umweg über Dateideskriptoren,
den epoll+timerfd nehmen, ist auf Linux eine Notwendigkeit der Architektur, kein
Selbstzweck.

Gewartet wird bewusst über eine **Dauer** (`wait_for`) statt über einen absoluten
Zeitpunkt: `fml::TimePoint` und `std::chrono::steady_clock` müssen nicht dieselbe Uhr
sein. Die Restzeit wird in jeder Runde neu berechnet, deshalb sind vorzeitige Aufwacher
unschädlich.

**Folgerung darüber hinaus:** Solche Stellen sind gefährlicher als jeder Compilerfehler,
weil sie nichts melden. `scripts/find-silent-fallbacks.py` sucht sie deshalb
systematisch – Präprozessorketten über mindestens zwei Plattformmakros, deren `#else`
einen Leerwert liefert. Der Lauf über `fml`, `shell`, `runtime`, `common`, `assets` und
`lib/ui` ergab **drei** Treffer: diesen einen echten, und zwei Fehlalarme
(`synchronization/semaphore.cc` – der `#else` ist dort der reguläre POSIX-Zweig;
`lib/ui/painting/image_generator_registry.cc` – eine zusätzliche macOS-Factory neben
der von Skia).

**Nebenbefund zu Semaphoren:** `sem_init`, `sem_wait`, `sem_trywait`, `sem_post`,
`sem_timedwait` sind in `libsysbase.a` **echt implementiert**, keine ENOSYS-Stubs. Das
Disassemblat von `sem_init` im gelinkten Programm prüft die Argumente, lehnt
`pshared != 0` mit `EINVAL` ab und legt den Zähler ab. fml übergibt `pshared = 0`.

---

## 2026-08-10 – Verzeichnis-Deskriptoren gibt es auf Horizon nicht

**Befund:** `fml/platform/posix/file_posix.cc` ist durchgängig fd-relativ gebaut: Ein
Verzeichnis wird einmal geöffnet, alles Weitere läuft über `openat` (Z. 93, 119),
`mkdirat` (Z. 112), `unlinkat` (Z. 167, 175), `faccessat` (Z. 187), `renameat` (Z. 231)
und `fdopendir` (Z. 242) relativ zu diesem Deskriptor.

Horizon kennt dieses Modell nicht. Die devoptab-Schnittstelle
(`devkitA64/aarch64-none-elf/include/sys/iosupport.h`) trennt beide Welten strikt:
`open_r` (Z. 39) liefert einen Dateideskriptor, `diropen_r` (Z. 54) einen `DIR_ITER*`.
Zwischen beiden gibt es keine Brücke – `struct DIR` (`dirent.h:35-39`) enthält einen
`DIR_ITER*`, keinen Deskriptor. Ein Verzeichnis-fd ist hier nicht unimplementiert,
sondern im Datenmodell nicht vorgesehen.

Passend dazu: newlib **deklariert** alle *at-Funktionen in den Headern, **definiert**
aber keine einzige. Geprüft über `nm --defined-only` gegen `libc.a`, `libsysbase.a`,
`libg.a` und `libnx.a` (`scripts/probe-at-funcs.sh`): `mkdirat`, `unlinkat`, `openat`,
`renameat`, `faccessat`, `fdopendir`, `dirfd` fehlen alle; die pfadbasierten
Entsprechungen `mkdir`, `unlink`, `rmdir`, `rename`, `access`, `opendir`, `readdir`
sind vorhanden.

**Konsequenz:** `embedder/src/platform/posix_compat_horizon.cpp` vergibt Verzeichnissen
Handles aus einem eigenen Zahlenbereich ab `0x40000000` und merkt sich zu jedem den
vollständigen Pfad. Die *at-Funktionen setzen daraus einen Pfad zusammen und rufen die
pfadbasierte Variante auf.

Weil `dup`, `close` und `fstat` ebenfalls auf solche Handles treffen (`file_posix.cc`
Z. 236, Z. 133, sowie jeder `UniqueFD`-Destruktor), laufen sie über `-Wl,--wrap=…`.
Für echte Deskriptoren reichen die Wrapper unverändert an libc weiter.

Zwei Dinge sind dabei bewusst *nicht* nachgebaut:

- **`dirfd()`** gibt `ENOTSUP` zurück. Aus einem `DIR_ITER` lässt sich kein Deskriptor
  gewinnen; ein erfundener Wert fiele erst später und an anderer Stelle auf.
- **Die Sicherheitsgarantie fd-relativer Zugriffe** – dass das Basisverzeichnis zwischen
  Öffnen und Benutzen nicht ausgetauscht werden kann – geht verloren. Auf einer Konsole
  ohne Mehrbenutzerbetrieb ist das hinnehmbar, aber es ist ein Verhaltensunterschied.

Die Schicht liegt im Embedder, nicht im Engine-Baum: Sie füllt eine Lücke der Plattform
und ändert kein Verhalten der Engine. Damit sinkt zugleich die Patchfläche – die frühere
`dart/bin/posix_at_horizon.cc`, die nur `AT_FDCWD` beherrschte, ist entfallen.

---

## 2026-08-10 – Idempotenz bricht, wenn sich der Ersatztext ändert

**Befund:** `replace_once` prüft zuerst, ob `new` schon im Text steht. Wird `new`
nachträglich geändert – hier: `posix_at_horizon.cc` aus der Einfügung entfernt –, greift
diese Prüfung nicht mehr, während der Anker (`"stdio_linux.cc",`) die erste Ersetzung
überlebt hat. Der Patch lief ein zweites Mal und trug `stdio_horizon.cc` doppelt ein,
was GN als doppelte Quelldatei ablehnt.

**Konsequenz:** Bei Einfügungen, deren Anker erhalten bleibt, reicht die generische
Prüfung nicht. Dort geht der Prüfung auf `new` eine eigene Prüfung auf den *Eintrag
selbst* voraus. Allgemeiner: Ein geänderter Ersatztext in einem idempotenten Patchskript
ist immer verdächtig, weil er die Wiedererkennung des bereits erledigten Zustands
zerstört.

---

## 2026-08-10 – `GetCachesDirectory` bleibt leer, und das ist richtig so

**Befund:** Einziger Aufrufer im Softwarepfad ist der `PersistentCache`
(`common/graphics/persistent_cache.cc:144`), der dort vorkompilierte Shader ablegt. Die
übrigen Aufrufer sind sämtlich Vulkan-Backends. Linux (`paths_linux.cc:22`) und QNX
(`paths_qnx.cc:15`) geben beide `{}` zurück.

**Konsequenz:** `paths_horizon.cc` tut dasselbe. Ein Shader-Cache hätte im
Software-Renderer nichts zu speichern und würde nur Dateien auf der SD-Karte anlegen.
`GetExecutablePath()` gibt `{false, ""}` zurück – Horizon führt kein `/proc`, und der
Pfad der laufenden NRO landet in `argv`, also außerhalb der Reichweite von fml.

---

## 2026-08-09 – Toolchain über Container statt lokaler Installation

**Befund:** Die lokale devkitPro-Installation scheiterte an einer sudo-Passwortabfrage, die
sich nicht automatisieren ließ. Das offizielle Image `devkitpro/devkita64` (2,79 GB)
enthält alles Nötige: devkitA64 r29.2-1 mit GCC 15.2.0, binutils 2.45.1, newlib 4.6.0,
libnx 4.12.0, switch-tools 1.13.1, general-tools 1.4.4.

**Konsequenz:** `scripts/dkp.ps1` hängt das Repo als `/work` in den Container und führt
darin einen Befehl aus. Kein root, keine Installation, und alle Beteiligten bauen gegen
exakt dieselben Compilerversionen – bei einer Portierung, bei der Compilerverhalten
selbst zur Fehlerquelle wird, ist das mehr wert als die Bequemlichkeit.

Der Referenz-Checkout unter `third_party/libnx` wurde von `master` auf Tag `v4.12.0`
umgestellt, damit die Header, gegen die ich prüfe, denen im Image entsprechen.

---

## 2026-08-09 – `hello_libnx` läuft auf echter Hardware

**Befund:** Erster Build ohne Compilerfehler, sauberer Rebuild ohne eine einzige Warnung
bei `-Wall`. Ergebnis: 214 KB, `NRO0`-Magic an Offset 0x10 vorhanden. Übertragung per
`nxlink` auf die Konsole, gestartet aus hbmenu im Applet-Modus. Auf dem Bildschirm
erscheint das erwartete Bild, der Balken wandert, A färbt die Fläche.

**Konsequenz:** Die gesamte Kette steht – Quelltext, Container-Toolchain, `.elf`, `.nro`,
Übertragung, Start, Framebuffer, Eingabe. Damit ist die Plattformschicht, auf der der
Embedder aufsetzt, kein Papier mehr.

**Zur Übertragung, zwei Stolpersteine:**

1. `nxlink` aus dem Container braucht `--network host`. Ohne den Schalter scheitert der
   Verbindungsaufbau („Connection to … failed") – die Docker-NAT verträgt den
   Netloader-Handshake nicht.
2. **Den Netloader-Port nicht vorher anpingen.** Ein `Test-NetConnection` auf Port 28280
   öffnet eine TCP-Verbindung, und der Netloader nimmt nur eine an – die Probe beendet
   die Sitzung, und der anschließende echte Upload scheitert. Direkt hochladen und
   `-r` für Wiederholungen nutzen.

**Applet-Modus-Eigenheit (nicht unser Code):** Beim ersten Start aus hbmenu erscheint
häufig „Error getting name length: err=11 / No more processes"; beim zweiten Versuch
klappt es. `err=11` ist `EAGAIN`, die Meldung stammt aus hbmenu selbst.

---

## 2026-08-09 – Meilenstein 2: `gn gen` bis zur Dart-VM vorgearbeitet

Jeder Fehler einzeln geklärt statt pauschal umgangen. Die Reihenfolge, in der GN sie
liefert, ist zugleich die Landkarte des Ports:

| # | Fehler | Ursache | Lösung |
|---|---|---|---|
| 1 | `Undefined identifier is_apple` (`BUILDCONFIG.gn:327`) | `current_os = "horizon"` traf keinen Zweig, also blieb jede `is_*`-Variable undefiniert | eigener Zweig plus `is_horizon = false` in den zehn vorhandenen |
| 2 | `Toolchain not set because of unknown platform` (`:678`) | keine Toolchain für das neue OS | `build/toolchain/horizon` nach QNX-Vorbild, `gcc_toolchain` mit `is_clang = false` |
| 3 | `vpython3` fehlt (exit 127) | GN ruft Hilfsskripte über depot_tools auf | depot_tools in den `PATH` des Build-Skripts |
| 4–7 | `engine_version`/`content_hash`/`skia_version`/`dart_version` fehlen | vier `assert` in `shell/version/version.gni:19-22` | Werte aus den Pins gesetzt; sie landen nur in Artefaktnamen |
| 8 | `Unknown/Unsupported platform` (`shell/platform/BUILD.gn:28`) | Gruppe `platform` kennt horizon nicht | `deps = []` wie bei QNX – der Embedder hängt separat unter `shell/platform/embedder` |
| 9 | `Unknown target_os: horizon` (`third_party/dart/runtime/BUILD.gn:155`) | **die Dart-VM** | offen – das ist der eigentliche Port |

**Bewertung:** Die Punkte 1 bis 8 waren Konfiguration, zusammen unter einer Stunde. Punkt 9
ist der Aufwand, den `docs/feasibility.md` §4 vorhergesagt hat. Ab hier wird nicht mehr
konfiguriert, sondern portiert.

### Fortsetzung: `gn gen` läuft, Übersetzung beginnt

| # | Fehler | Art | Lösung |
|---|---|---|---|
| 9 | `Unknown target_os: horizon` (Dart) | Konfiguration | `DART_TARGET_OS_HORIZON` in `runtime/BUILD.gn` |
| 10 | `Unsupported ARM OS` (zlib) | Plattformlücke | NEON/CRC32 für horizon aus – die OS-spezifische CPU-Erkennung (`getauxval`) fehlt |
| 11 | `pkg-config` fehlt | Umgebung | ohne root aus dem Container-Image nach `~/bin` |
| — | **`gn gen` fertig: 918 Targets** | | |
| 12 | `-Xclang`, `-fno-cxx-modules` | Toolchain | nur noch unter `is_clang` |
| 13 | `#error Please add support for your platform` | Quellcode | `FML_OS_HORIZON` über `__SWITCH__` |
| 14 | `control reaches end of non-void function` | Toolchain | `-Werror` aus, wie QNX es schon tut |
| 15 | `ULONG_MAX` nicht deklariert | Portabilität | `<climits>` ergänzt |
| 16 | `execinfo.h` fehlt | **Plattformlücke** | offen – glibc-Backtraces gibt es auf Horizon nicht |

**`-Werror`:** QNX nimmt sich davon bereits aus (`compiler/BUILD.gn:749`), und aus genau
demselben Grund – die Warnungsfreiheit der Engine ist gegen Clang geprüft, nicht gegen GCC.
GCC meldet etwa nach einem `switch` über sämtliche Enum-Werte trotzdem
„control reaches end of non-void function" (`fml/cpu_affinity.cc:86`). Das ist eine
Bootstrap-Maßnahme, kein Dauerzustand.

**Beobachtung zur Toolchainwahl:** Die Fehler 12 und 14 wären mit Clang nicht aufgetreten.
Sollte sich diese Klasse häufen, ist der `custom_toolchain`-Weg aus
`docs/gn-target-horizon.md` die Alternative – Clang gegen das devkitA64-Sysroot statt GCC.
Bisher sind es zwei Fälle; das rechtfertigt den Wechsel noch nicht.

### fml übersetzt vollständig

| # | Fehler | Art | Lösung |
|---|---|---|---|
| 16 | `execinfo.h` fehlt | Plattformlücke | Backtrace liefert für Horizon null Frames – leer statt erfunden |
| 17 | `sys/mman.h` in `mapping_posix.cc` | Plattformlücke | eigenes `mapping_horizon.cc`: Datei in Heap-Puffer lesen |
| 18 | `sys/mman.h` in `file_posix.cc` | toter Code | Include entfernt, wird dort nirgends benutzt |
| 19 | `dlfcn.h` | Plattformlücke | eigenes `native_library_horizon.cc`, schlägt sichtbar fehl |
| 20 | `::strdup` nicht deklariert | Toolchain | `-std=gnu++20` statt `-std=c++20`; strenges ANSI blendet newlibs POSIX-Erweiterungen aus |
| 21 | `sys/mman.h` in ICU | Plattformlücke | `U_HAVE_MMAP=0` → ICU nimmt seinen eigenen `MAP_STDIO`-Pfad |
| 22 | `static_cast<pid_t>(pthread_t)` | Plattformunterschied | `pthread_t` ist auf Horizon ein Zeiger; eigener `GetTID`-Zweig in abseil |
| 23 | `link.h` fehlt | Plattformlücke | `__SWITCH__` in die Ausschlussliste von `ABSL_HAVE_ELF_MEM_IMAGE` |

**Ergebnis:** 43 Objektdateien, `ELF64 / AArch64 / REL`, zweiter Lauf meldet
`ninja: no work to do`. Damit übersetzt die Betriebssystemabstraktion der Flutter Engine
für Horizon.

**Muster, das sich abzeichnet:** Fast jede Lücke hat im Upstream bereits einen vorgesehenen
Ausweg, weil andere schlanke Plattformen dasselbe Problem hatten. ICU hat `MAP_STDIO`,
abseil hat eine Ausschlussliste, in der QNX, Haiku und VxWorks schon stehen. Es lohnt sich
also, vor jeder eigenen Lösung zu prüfen, ob der Code den Fall nicht schon kennt.

**Die drei eigenen Implementierungen** liegen unter `patches/flutter-engine/files/` und
werden vom Patch-Skript in den Checkout kopiert. Die wichtigste Abweichung: `FileMapping`
liest die Datei vollständig in den Heap statt sie abzubilden. Das kostet Speicher und
verhindert schreibbare Abbildungen – Letzteres schlägt bewusst mit Protokolleintrag fehl,
statt eine abweichende Zusicherung zu liefern.

### Die Dart-VM übersetzt – und was das nicht heißt

Die vier plattformabhängigen Header waren weniger Arbeit als erwartet: `platform/threads.h`
und `platform/synchronization.h` brauchten nur einen zusätzlichen Zweig, weil libnx über
die Newlib-Syscalls echte pthreads liefert. `platform/utils.h` und `vm/os_thread.h`
bekamen eigene Dateien.

`utils_horizon.h` kommt ohne `<endian.h>` und `<byteswap.h>` aus – newlib hat beides
nicht – und benutzt stattdessen `__builtin_bswap*` mit `__BYTE_ORDER__`. `strerror_r` ist
ebenfalls nicht deklariert; `strerror` ist vertretbar, weil newlib für bekannte Codes
konstante Zeichenketten liefert und der Rückfallpuffer in der pro Thread geführten
Reentrancy-Struktur liegt.

**Ein Konfigurationsfehler, der lange unbemerkt lief:** Der Build lief bis hierher im
Debug/JIT-Modus (`-DFLUTTER_RUNTIME_MODE=1`, `-DFLUTTER_JIT_RUNTIME=1`). Erst
`flutter_runtime_mode="release"` **und** `dart_runtime_mode="release"` schalten auf
Release/AOT. Letzteres ist ein eigenes GN-Argument mit Vorgabe `"develop"`, das die
Flutter-Seite nicht automatisch mitsetzt. Nebenwirkung: Dart schließt damit Perfetto und
den Profiler aus, die beide auf Horizon nicht übersetzen.

**Ergebnis:** `libdart_vm_aotruntime_product` übersetzt vollständig – 675 Ziele, 184
Objektdateien, keine Fehler.

**Und was das nicht heißt.** Dart übersetzt *alle* `os_*.cc`, aber jede verbirgt ihren
Inhalt hinter `#if defined(DART_HOST_OS_...)`. Für Horizon sind `os_linux.o`, `os_win.o`
und Verwandte also leere Objektdateien. Es gibt schlicht kein `os_horizon.cc`.

**Korrektur der ersten Messung.** Zunächst hatte ich nur die undefinierten Symbole
gezählt – das überschätzt deutlich, weil Querverweise zwischen Objektdateien der Normalfall
sind. Nach Abzug der anderswo definierten Symbole (1739 undefiniert, davon 1177 anderswo
definiert) bleibt:

| Bereich | fehlende Funktionen |
|---|---|
| `dart::OS::` | 16 |
| `dart::OSThread::` | 11 |
| `dart::VirtualMemory::` | 7 |
| `dart::CpuInfo` | 5 |
| `dart::NativeSymbolResolver` | 5 |
| `dart::ThreadInterrupter` | **0** |

44 Plattformfunktionen. Gegenüber der ersten Schätzung sind `CpuInfo` und
`NativeSymbolResolver` hinzugekommen, während ein Teil der `OSThread`-Funktionen bereits
in `os_thread.cc` definiert war. Dass `ThreadInterrupter` bei null steht, bestätigt die
frühere Annahme: Der signalbasierte Profiler entfällt im Product-Modus tatsächlich.

Die übrigen 562 offenen Symbole stammen aus anderen Zielen (`dart::Api::`,
`dart::BaseTextBuffer::` und Verwandte) und lösen sich beim Zusammenlinken auf.

### Die Engine übersetzt vollständig – und muss statisch gelinkt werden

**3184 Objektdateien, AArch64 ELF64.** Alle Kernsymbole der Embedder-API sind vorhanden.

Der Linkschritt scheiterte allerdings – und der Grund ist architektonisch, nicht technisch:
Das Ziel `flutter_engine_library` ist eine **`shared_library`**. Horizon-Homebrew kennt
keine dynamischen Bibliotheken, und devkitA64s eigene Bibliotheken sind folgerichtig nicht
mit `-fPIC` übersetzt:

```text
relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol `devoptab_list'
which may bind externally can not be used when making a shared object
```

`libsysbase` ist die Newlib-Anbindung von devkitPro – sie *kann* gar nicht in eine Shared
Library, weil es auf dieser Plattform keine gibt.

**Konsequenz:** Die Engine wird als Objektsammlung bzw. statische Bibliothek gebaut und in
die `.nro` gelinkt. Daneben steht dafür bereits
`flutter/shell/platform/embedder:embedder_as_internal_library` – ein `source_set`, das
genau dies liefert und ohne Änderung durchläuft.

Das passt zu allem, was diese Portierung bisher ergeben hat: kein `dlopen`, keine
Laufzeit-Symbolauflösung, keine nativen Assets, keine Shared Libraries. **Alles liegt in
der NRO** – einschließlich, wie in Meilenstein 1b gezeigt, des Dart-AOT-Snapshots.

Zwei Fehler wurden beim Linken sichtbar, die der Compiler nicht sehen konnte:
`OSThread::GetCurrentStackPointer()` war doppelt definiert (die Funktion steht bereits
plattformunabhängig in `os_thread.cc`), und zwei weitere Stellen hängten `-ldl` an –
`flutter/skia/BUILD.gn`, wo QNX bereits ausgenommen war, und `dart/runtime/bin/BUILD.gn`.

### dart:io – Umfang vermessen und Strategie festgelegt

Der Build erreichte `dart/runtime/bin`, die native Seite von `dart:io`: **16
plattformspezifische Dateien, 4671 Zeilen**.

Statt sie zu kopieren wurde zuerst geprüft, welche überhaupt Linux-*spezifische* APIs
benutzen. Ergebnis: `thread_linux.cc`, `utils_linux.cc` und `socket_base_linux.cc`
benutzen **gar keine** – sie sind reiner POSIX-Code unter einem Linux-Wächter.

Deshalb der Ansatz: **Wächter erweitern statt kopieren.** Bei zehn Dateien wurde
`DART_HOST_OS_HORIZON` in die vorhandene Bedingung aufgenommen. Das hält die Abweichung
vom Upstream klein und macht sichtbar, wo Horizon sich wirklich unterscheidet – nämlich
nur dort, wo eine eigene Datei entsteht.

Der Ansatz ist bewusst empirisch: Welche Datei hineingehört, entscheidet der Compiler.
Das ist erheblich schneller, als 4671 Zeilen vorab zu bewerten.

**Ergebnis des ersten Durchlaufs:** Die zehn `.cc`-Dateien übersetzen. Es scheitern nur
die Header, die ihre plattformspezifischen Gegenstücke auswählen – `socket_base.h`
(gelöst, `socket_base_linux.h` umfasst 17 Zeilen reiner Systemincludes) und
`eventhandler.h`.

### Der Eventhandler und sein Weckmechanismus

`eventhandler_linux.cc` (433 Zeilen) setzt **epoll** und **timerfd** voraus, die Horizon
nicht hat. Eine `poll`-basierte Fassung ist der Weg – aber sie hängt an einer Frage, die
vor dem Schreiben zu klären war: Womit weckt ein anderer Thread den wartenden `poll`?
Linux benutzt dafür eine Pipe (`interrupt_fds_[2]`).

Geprüft, was libnx tatsächlich definiert (`nm` über `libnx.a`):

| Funktion | vorhanden |
|---|---|
| `poll` | ja |
| `select` | ja |
| `socketpair` | **ja** |
| `pipe` | **nein** |

**Entscheidung:** `socketpair()` statt Pipe. Ein verbundenes Socket-Paar erfüllt denselben
Zweck – ein Deskriptor, auf den `poll` warten kann und den ein anderer Thread beschreibt –
und ist auf Horizon verfügbar. Der Timer läuft nicht über `timerfd`, sondern über das
Zeitlimit von `poll` selbst, berechnet aus der vorhandenen `TimeoutQueue`.

### Die Dart-Portierung steht

Fünf Dateien, 44 Funktionen, alle Plattformgruppen auf null offene Symbole:

| Datei | Ansatz |
|---|---|
| `os_horizon.cc` | POSIX-Zeitfunktionen (`clock_gettime`, `nanosleep`), stdio. Bewusst **ohne** `<switch.h>`, dessen Makros und Typnamen (`u32`, `Result`) mit Dart-Bezeichnern kollidieren. |
| `os_thread_horizon.cc` | pthreads, eng an der Linux-Fassung |
| `virtual_memory_horizon.cc` | `memalign` aus dem Prozess-Heap, siehe unten |
| `cpuinfo_horizon.cc` | kein `/proc/cpuinfo`; `HasField` meldet `false`, worauf `GetCpuModel()` von selbst auf „Unknown" fällt |
| `native_symbol_horizon.cc` | keine Symbolauflösung zur Laufzeit; alle Anfragen melden „nicht gefunden" |

**Zwei bewusst offen gelassene Lücken**, beide im Code markiert:

`OSThread::GetCurrentStackBounds` liefert `false`. Linux ermittelt die Grenzen über
`pthread_getattr_np`, das newlib nicht hat; libnx kennt sie zwar, aber der Zugriff würde
`<switch.h>` in die Dart-VM ziehen. Die VM wertet `false` als „unbekannt" und fällt bei der
Erkennung von Stapelüberläufen auf konservativere Verfahren zurück. Sollte das zum Problem
werden, ist das die Stelle.

`OS::GetCurrentThreadCPUMicros` liefert `-1`, weil `CLOCK_THREAD_CPUTIME_ID` fehlt. Die VM
wertet das als „nicht verfügbar" aus – ein erfundener Wert wäre schlechter als keiner.

**Ein eigener Fehler, der Zeit kostete:** Meine Idempotenzprüfung im Patch-Skript prüfte
nach dem Fix zuerst den Anker statt das Ergebnis. Beim *Einfügen* bleibt der Anker aber
stehen, also wurden alle fünf Einträge in `vm_sources.gni` doppelt eingetragen und `gn gen`
brach ab. Die Prüfung muss zuerst auf das Ergebnis schauen – aber nur, wenn dieses nicht
leer ist, sonst gilt eine Löschung immer als erledigt. Beide Fälle sind jetzt abgedeckt.

### Entscheidung zum Speichermodell

`virtual_memory_horizon.cc` nimmt den einfachsten Weg, der die Zusicherungen erfüllt:
ausgerichtete Anforderungen über `memalign` aus dem Prozess-Heap. Damit fallen Reserve und
Commit zusammen, `Protect` bleibt wirkungslos und `FreeSubSegment` meldet ehrlich `false`.

Im AOT-Product-Modus ist das vertretbar: Ausführbarer Speicher wird nicht gebraucht – die
Snapshot-Instruktionen liegen in der `.text` der NRO –, und Schreibschutz für Code-Seiten
betrifft nur zur Laufzeit erzeugten Code, den es hier nicht gibt.

**Damit ist die 4-GB-Frage entschieden, und zwar gegen Compressed Pointers.** Deren
Reservierung würde mit dieser Fassung 4 GB tatsächlich belegen statt nur Adressraum zu
reservieren – auf einer Konsole mit 4 GB RAM aussichtslos. Der Build setzt deshalb
`dart_use_compressed_pointers=false`, und die Datei bricht mit `#error` ab, falls diese
Verbindung je unbemerkt zerfällt.

Das ist eine bewusste Bootstrap-Entscheidung, kein Endzustand: libnx hat mit `virtmem` und
`svcMapMemory` die Bausteine für eine echte Trennung von Reservierung und Abbildung. Ohne
Compressed Pointers kostet Dart mehr Speicher pro Objektzeiger – was auf einem Gerät mit
knappem RAM irgendwann zurückkommt.

**Fehler 15 und 16 sind verschiedene Dinge.** `<climits>` ist eine echte
Portabilitätslücke im Upstream-Code, die auf jeder schlanken libc auffiele – newlib zieht
weniger transitiv herein als glibc. `execinfo.h` dagegen ist eine Funktion, die Horizon
schlicht nicht hat.

**Zwei Lehren aus eigenen Fehlern:** Das Einfügen von GN-Zweigen per Index-Schneiden hat
zweimal die schließende Klammer verschluckt, weil der eingefügte Block die ersetzte
Klammer selbst mitbringen muss. Seither nur noch wörtliche Textersetzung mit vollständigem
Kontext. Und: `docker run ... > datei.tar` über PowerShell zerstört Binärdaten – der
Container muss direkt in ein gemountetes Verzeichnis schreiben.

**devkitA64 in WSL ohne root:** `docker run --rm -v "E:\:/out" devkitpro/devkita64 tar -cf
/out/dkp.tar -C /opt devkitpro`, danach in WSL entpacken. 1,2 GB, GCC 15.2.0 läuft nativ.
Damit ist die sudo-Blockade endgültig umgangen.

---

## 2026-08-09 – Meilenstein 1b: Der AOT-Ladeweg trägt

**Die zentrale Hypothese des Projekts ist bis zur Linkphase bestätigt.**

Kette, alle Schritte tatsächlich ausgeführt:

```text
hello.dart
  → gen_kernel (dartaotruntime + vm_platform_product.dill)  → hello.dill, 4,1 MB
  → gen_snapshot --snapshot_kind=app-aot-assembly           → hello_aot.s, 2,47 MB
                                                               121.924 Zeilen
  → aarch64-none-elf-gcc -c (devkitA64 15.2.0)              → hello_aot.o
  → Link mit devkitPro-Regeln                               → aot_poc.nro, 758 KB
```

**Befund 1 – `gen_snapshot` muss nicht selbst gebaut werden.** Die
Android-arm64-Release-Artefakte des gepinnten Flutter-SDK enthalten einen
`gen_snapshot`, der auf x64 läuft und AArch64 erzeugt. Für die *Assembly*-Ausgabe ist
das Zielbetriebssystem unerheblich, nur die Zielarchitektur zählt. Das spart im
PoC einen kompletten Dart-SDK-Build.

**Befund 2 – devkitA64 übersetzt die Ausgabe unverändert.** Die Assembly verwendet
ausschließlich Standard-GNU-Direktiven (`.quad`, `.byte`, `.balign`, `.globl`,
`.type … %object`, `.size`, `.uleb128`, `.cfi_*`). Keine Anpassung, kein Patch, keine
Warnung.

**Befund 3 – die Instruktionen landen im ausführbaren Segment.** `nm` auf der fertigen
ELF:

```text
00000000000003c0 T _kDartVmSnapshotInstructions
0000000000016e00 T _kDartIsolateSnapshotInstructions
0000000000078500 R _kDartVmSnapshotData
000000000007cd00 R _kDartIsolateSnapshotData
```

`T` bedeutet `.text`. Damit liegt der AOT-Code in dem Bereich, den der Homebrew-Loader
ohnehin ausführbar mappt – **kein `dlopen`, kein `mmap`, kein `mprotect`, kein `jit.h`**.
Genau das war in `docs/feasibility.md` §1 als Hypothese formuliert.

**Befund 4 – Namensfalle für den anderen Weg.** `gen_snapshot` erzeugt die Symbole mit
führendem Unterstrich (`_kDartVmSnapshotData`), die Engine sucht per dlsym dagegen ohne
(`dart_snapshot.cc:18-21`). Für unseren Weg irrelevant, weil wir Zeiger übergeben statt
Namen – für den ELF-/`dlopen`-Weg wäre es eine stille Fehlerquelle gewesen.

**Befund 5 – auf Hardware bestätigt.** Die `.nro` auf der Konsole gestartet, Adressen und
erste Bytes zur Laufzeit ausgelesen:

```text
kDartVmSnapshotData              0x57f0ef740  f5 f5 dc dc cc 41 00 00
kDartVmSnapshotInstructions      0x57f06ec40  20 6a 01 00 00 00 00 00
kDartIsolateSnapshotData         0x57f0f3f40  f5 f5 dc dc ff 6b 02 00
kDartIsolateSnapshotInstructions 0x57f085680  a0 f9 03 00 00 00 00 00
Adresse von main()               0x57f06e180
```

Zwei Dinge daran zählen:

`f5 f5 dc dc` ist little-endian `0xdcdcf5f5` – die Snapshot-Magic der Dart-VM
(`runtime/vm/snapshot.h:37`). Genau diesen Wert liest `SnapshotHeaderReader::IsValid()`
als Erstes. Die Daten haben Übersetzung, Linken, NRO-Verpackung und Laden also unbeschädigt
überstanden.

`kDartVmSnapshotInstructions` liegt 0xAC0 Bytes hinter `main()`. Dart-AOT-Code und unser
C++-Code liegen damit im selben Modul und im selben ausführbar gemappten Bereich. Der
Homebrew-Loader hat den Snapshot ausführbar gemappt, ohne dass wir etwas dafür tun mussten.

**Was das ausdrücklich nicht zeigt:** dass der Snapshot *läuft*. Dafür fehlt die Dart-VM
für Horizon. Geprüft ist der Ladeweg, nicht die Ausführung. Der nächste Beweis dafür
kommt erst mit Meilenstein 2.

---

## 2026-08-09 – Logausgabe: Richtung umdrehen statt gegen NAT kämpfen

**Problem:** `nxlink -s` leitet stdout der Konsole zum Entwicklungsrechner um, indem die
Switch sich zum Absender *zurück*verbindet. Läuft nxlink in einem Container, ist der
Absender hinter NAT und nicht erreichbar – die Ausgabe kommt nie an. Mit
`--network host` funktioniert zwar der Upload, aber nicht der Rückkanal.

**Lösung:** Eine TCP-Senke im Logging, bei der die **Switch** die Verbindung aufbaut
(`LogConfig::remote_host`/`remote_port`). Ausgehende Verbindungen gehen durch jedes NAT.
Empfänger ist `scripts/log-listener.ps1`.

Zwei Eigenschaften, die sich beim Debuggen auszahlen: Der Verbindungsaufbau hat zwei
Sekunden Zeitlimit, damit ein nicht laufender Empfänger nicht den Anwendungsstart
blockiert. Und bricht die Verbindung im Betrieb weg, legt sich nur diese Senke still,
während Datei und Konsole weiterlaufen.

Die Senke sitzt im Embedder-Logging, nicht im Beispiel – der spätere Flutter-Host bekommt
sie damit geschenkt, inklusive der Engine-Ausgabe über `FlutterLogMessageCallback`.

---

## 2026-08-09 – Falle: frischer `PadState` meldet gehaltene Tasten als Neudruck

**Befund:** Ein neu per `padInitializeDefault` angelegter `PadState` startet mit leerem
Vorzustand (`buttons_old == 0`). Ist eine Taste beim ersten `padUpdate` noch physisch
gedrückt, meldet `padGetButtonsDown` sie als *neuen* Druck.

**Symptom:** Der Diagnoseschirm von `hello_libnx` verschwand sofort wieder – er wird nach
einem Plus-Druck geöffnet, und genau dieses noch gehaltene Plus beendete ihn im selben
Moment. Auf dem Fernseher: kurz schwarz, dann hbmenu.

**Konsequenz:** Nach dem Anlegen eines `PadState` einmal `padUpdate` zum Einlesen des
Startzustands aufrufen und, wenn ein Tastendruck den Kontextwechsel ausgelöst hat, erst
die Freigabe abwarten. Für den späteren Flutter-Eingabepfad ist dieselbe Falle relevant:
Pointer- und Key-Events dürfen nicht aus einem uninitialisierten Vorzustand abgeleitet
werden, sonst erzeugt jeder Fokuswechsel Phantom-Events.

**Noch offen:** Logausgabe wurde noch nicht gesehen, sauberes Beenden über Plus noch nicht
bestätigt, und der Framebuffer-Stride ist noch nicht gegen `width * 4` geprüft. Genau
dieser Wert entscheidet später, ob Flutters Zeilenabstand direkt durchgereicht werden kann.

---

## 2026-08-09 – devkitPro baut standardmäßig ohne RTTI und ohne Exceptions

**Befund:** Das offizielle Anwendungs-Template setzt
`CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions`
(`third_party/reference/switch-application.Makefile:57`). Ebenso bestätigt es, dass
`-D__SWITCH__` aus dem Makefile kommt (`:55`) und nicht vom Compiler – siehe
`docs/gn-target-horizon.md`.

**Konsequenz:** Die Coding-Regel „keine Exceptions voraussetzen, bevor geprüft ist, ob wir
sie im Ziel-Build wollen" ist damit beantwortet: Der Homebrew-Standard ist ohne. Unser
Embedder hält sich daran. Für den Engine-Build heißt das, dass Engine, Dart-VM und Skia
mit denselben Einstellungen gebaut werden müssen – gemischte Übersetzungseinheiten
(einige mit, andere ohne Exceptions) sind eine der unangenehmeren Fehlerquellen beim
Linken. Zu prüfen, sobald der Engine-Build steht.

---

## 2026-08-09 – Dart braucht im AOT-Modus keinen ausführbaren Speicher (mit einer Ausnahme)

Geprüft gegen `dart-lang/sdk` @ `02abc578` (Shallow-Checkout unter `~/dart-sdk-ref` in WSL).

**Befund 1 – Snapshot-Instruktionen:** `PageSpace::SetupImagePage`
(`runtime/vm/heap/pages.cc:1527-1560`) legt für die AOT-Instruktionen eine Page über
`VirtualMemory::ForImagePage(pointer, size)` an. Solche Regionen haben
`vm_owns_region() == false` – die VM ändert weder Schutz noch gibt sie sie frei
(`runtime/vm/virtual_memory.h:143-148`). Aufgerufen wird das aus
`app_snapshot.cc:9987-10096` mit `is_executable=true`, aber ohne jede Allokation.

**Konsequenz:** In die `.text` der NRO gelinkte Instruktionen sind genau das, was die VM
hier erwartet. Kein `mmap`, kein `mprotect`, kein `PROT_EXEC` zur Laufzeit.
Einzige Bedingung: `ShouldDualMapExecutablePages()` muss `false` sein – das ist es, weil
`DART_ENABLE_RX_WORKAROUNDS` nur für iOS+JIT definiert wird (`virtual_memory.h:19-40`).

**Befund 2 – Heap:** Ausführbare Heap-Pages (`PageSpace::AllocatePage(is_exec=true)`)
entstehen nur beim Kompilieren zur Laufzeit, also nicht im Precompiled Runtime.

**Befund 3 – FFI-Callbacks (die Ausnahme):** `FfiCallbackMetadata` allokiert
Trampolin-Seiten und flippt sie außerhalb von macOS per `Protect()` von RW nach RX
(`runtime/vm/ffi_callback_metadata.cc:152-165`). Das passiert aber **lazy**:
`FfiCallbackMetadata::Init()` (`dart.cc:393`) legt nur das Singleton an
(`ffi_callback_metadata.cc:103-107`); die Seite entsteht erst in
`EnsureFreeListNotEmptyLocked()`, wenn tatsächlich ein `NativeCallable` erzeugt wird.

**Konsequenz:** Eine Flutter-App ohne FFI-Callbacks braucht nie ausführbaren Speicher.
Apps mit `NativeCallable` brauchen ihn – und Horizons `jit.h` bietet ein RW/RX-*Doppel*mapping,
während Dart hier das Umschalten *einer* Adresse erwartet. Das ist eine dokumentierte
Einschränkung für später, kein MVP-Blocker.

---

## 2026-08-09 – Compressed Pointers verlangen 4 GB Adressraum

**Befund:** Bei `DART_COMPRESSED_POINTERS` reserviert `VirtualMemory::Init()`
`kCompressedHeapSize` mit `kCompressedHeapAlignment` und bricht sonst mit `FATAL` ab
(`runtime/vm/virtual_memory_posix.cc:565-577`). Beide Konstanten sind **4 GB**
(`runtime/vm/virtual_memory_compressed.h`). Auf arm64 sind Compressed Pointers der
Standard.

**Konsequenz:** Entweder eine 4-GB-ausgerichtete 4-GB-Reservierung im Horizon-Adressraum
(reine Buchhaltung wäre über libnx `virtmem.h` denkbar, da dort Reservierung und
tatsächliches Mapping ohnehin getrennt sind), oder Dart mit
`dart_use_compressed_pointers=false` bauen – kostet Speicher, entschärft aber ein Risiko.
Entscheidung fällt, sobald der Engine-Build steht; vorher nicht spekulieren.

**Nebenbefund:** Dart trennt `Reserve()` / `Commit()` / `Decommit()`
(`virtual_memory.h:157-159`). Diese Aufteilung passt gut zu libnx, wo
Adressraum-Buchhaltung (`virtmem`) und echtes Mapping (`svcMapMemory`) ebenfalls getrennt sind.

---

## 2026-08-09 – Engine-Quellen liegen lokal vor

**Befund:** Das installierte Flutter-SDK (`C:\Users\mirkorichter\flutter`, Tag `3.41.6`)
ist das Monorepo und enthält `engine/src/flutter` vollständig, inklusive `build/`-Konfiguration.
Es fehlen nur die `gclient`-Abhängigkeiten (Dart SDK, Skia, buildtools).

**Konsequenz:** Engine-Analyse ist offline und exakt gegen die gepinnte Version möglich.
Für den eigentlichen Build braucht es trotzdem einen separaten `gclient`-Checkout auf dem
Linux-Host, weil der SDK-Checkout kein `.gclient` besitzt.

---

## 2026-08-09 – AOT-Snapshot per Symbolreferenz wird ausgewertet

**Befund:** `FlutterProjectArgs.vm_snapshot_instructions` &co. werden in
`shell/platform/embedder/embedder.cc:1817-1837` in `Settings` übertragen und in
`runtime/dart_snapshot.cc:102-177` über den Embedder-Callback aufgelöst, *vor* Dateipfaden
und vor `fml::NativeLibrary` (`dlopen`).

**Konsequenz:** Der ELF-/`dlopen`-Weg (`kFlutterEngineAOTDataSourceTypeElfPath`, der
einzige Typ in `FlutterEngineAOTDataSource`) ist vermeidbar. Siehe `docs/feasibility.md` §1.

---

## 2026-08-09 – QNX als Vorlage für ein neues Target-OS

**Befund:** `engine/src/build/config/BUILDCONFIG.gn:313-325` definiert `current_os == "qnx"`
mit eigenem `is_*`-Block; `fml/BUILD.gn:199-201` fügt für QNX genau eine Quelldatei hinzu.
Betroffen sind insgesamt ~11 Dateien (4 in `build/`, 7 `.gn` in `flutter/`).

**Konsequenz:** `current_os = "horizon"` ist ein kleiner, sauber isolierbarer Patch.
Patches gehören nach `patches/flutter-engine/`.

---

## 2026-08-09 – libnx liefert echte pthreads

**Befund:** `nx/source/runtime/newlib.c` implementiert `__syscall_thread_create`,
`__syscall_thread_join`, `__syscall_thread_exit`, `__syscall_thread_self` auf libnx-`Thread`.
Thread-Stacks müssen 4K-aligned sein (ebd. Zeile 166).

**Konsequenz:** `fml` kann die POSIX-Threadschicht nutzen. Die Dart-VM braucht dennoch
eigene `os_thread_horizon.{cc,h}`, weil sie keine generische POSIX-Variante besitzt
(nur `os_thread_absl` als Alternative, Abseil ist auf libnx nicht verfügbar).
