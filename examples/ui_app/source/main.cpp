// Erster Versuch, eine Dart-Anwendung auf der Switch laufen zu lassen.
//
// Gegenüber engine_link_test kommen zwei Dinge dazu, die den Unterschied
// zwischen "die Engine startet" und "die Engine hat etwas auszuführen"
// ausmachen:
//
//   1. Der AOT-Snapshot ist ins Programm gelinkt. Die Engine bekommt ihn über
//      FlutterProjectArgs gereicht, nicht über dlopen - das gibt es hier
//      nicht, und der Ausweichweg ist genau der, an dem der letzte Lauf
//      gescheitert ist.
//   2. Ein Software-Renderer, der Flutters Puffer in den Framebuffer schreibt.

#include <switch.h>

#include <dirent.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "embedder.h"

#include "flutter_libnx/log.h"
#include "flutter_libnx/switch_platform.h"
#include "flutter_libnx/task_runner.h"

// Aus thread_diag_horizon.cpp: meldet Heap-Regionen mit abweichendem
// Memory-State. Die Kontrollpunkte unten klammern ein, welcher Schritt
// (statische Konstruktoren, romfsInit, framebufferCreate, Engine-Start)
// Seiten im falschen Zustand hinterlaesst - die Verdaechtigen zum
// sporadischen _malloc_r-Absturz und zum threadCreate-Fehler 0xd401.
extern "C" void flutter_libnx_scan_heap(const char* label);
extern "C" void flutter_libnx_heal_heap(void);
extern "C" void flutter_libnx_fonts_cleanup(void);
extern "C" void flutter_libnx_random_cleanup(void);
// Aus plugins_horizon.cpp: path_provider und shared_preferences
// (StandardMethodCodec). true = Kanal wurde dort beantwortet.
extern "C" bool flutter_libnx_handle_plugin_message(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message);
// DIAGNOSE: sqlite3-Rauchprobe auf C-Ebene (sqlite3_probe_horizon.cpp).
// Der Fall (zwei devoptab-Fallen) ist geloest; der Aufruf unten bleibt
// auskommentiert fuer die naechste Datei-I/O-Fehlersuche.
extern "C" void flutter_libnx_sqlite3_probe(void);
// Aus textinput_horizon.cpp: Bildschirmtastatur ueber das swkbd-Applet.
extern "C" bool flutter_libnx_handle_textinput(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message);
// Aus egl_horizon.cpp: GPU-Rendering ueber Mesa/nouveau. Scheitert die
// Initialisierung, faellt main() auf den Software-Renderer zurueck.
extern "C" bool flutter_libnx_egl_init(void);
extern "C" void flutter_libnx_egl_shutdown(void);
extern "C" bool flutter_libnx_egl_make_current(void);
extern "C" bool flutter_libnx_egl_clear_current(void);
extern "C" bool flutter_libnx_egl_present(void);
extern "C" void* flutter_libnx_egl_resolve(const char* name);
// Aus media_kit_video_horizon.cpp: der Kanal com.alexmercerind/
// media_kit_video und die mpv-Render-Bruecke in externe Flutter-Texturen.
extern "C" bool flutter_libnx_handle_media_kit_video(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message);
extern "C" bool flutter_libnx_media_kit_texture_frame(
    void* user_data,
    int64_t texture_id,
    size_t width,
    size_t height,
    FlutterOpenGLTexture* texture_out);
extern "C" void flutter_libnx_media_kit_video_pump(
    FLUTTER_API_SYMBOL(FlutterEngine) engine);
extern "C" void flutter_libnx_media_kit_video_shutdown(void);

namespace {

constexpr const char* kHostIp = "192.168.0.153";
constexpr uint16_t kHostPort = 28800;

flutter_libnx::SwitchPlatform* g_platform = nullptr;

// Für den Platform-Message-Rückruf: Er bekommt nur user_data, aber zum
// Antworten braucht er die Engine. Gesetzt direkt nach FlutterEngineInitialize.
FLUTTER_API_SYMBOL(FlutterEngine) g_engine = nullptr;

// Ob psmInitialize gelang - ohne den Dienst antwortet der Batterie-Kanal
// mit einem Fehler statt mit erfundenen Werten.
bool g_psm_ready = false;

// Steht der erste Frame auf dem Bildschirm, hat `runApp()` sicher gebaut –
// der Anhaltspunkt, ab dem das View-Fokus-Ereignis einen Empfänger hat
// (Begründung am Sendeort in main()).
std::atomic<bool> g_first_frame_presented{false};

void NoteFramePresented();

// Flutter rendert in einen eigenen Puffer und reicht ihn hier herein. Weil der
// Framebuffer der Switch 5120 Bytes je Zeile hat und das genau 1280 * 4 ist,
// passt Flutters Zeilenlänge ohne Umrechnung - gemessen in Meilenstein 1.
bool SoftwarePresent(void* user_data,
                     const void* allocation,
                     size_t row_bytes,
                     size_t height) {
  if (g_platform == nullptr || allocation == nullptr) {
    return false;
  }

  uint32_t stride = 0;
  uint8_t* target = reinterpret_cast<uint8_t*>(g_platform->BeginFrame(&stride));
  if (target == nullptr) {
    return false;
  }

  const uint8_t* source = static_cast<const uint8_t*>(allocation);
  if (stride == row_bytes) {
    std::memcpy(target, source, row_bytes * height);
  } else {
    // Sollte auf 1280x720 nicht vorkommen; wenn doch, ist zeilenweises
    // Kopieren immer noch richtig - nur langsamer.
    for (size_t y = 0; y < height; ++y) {
      std::memcpy(target + y * stride, source + y * row_bytes, row_bytes);
    }
  }

  g_platform->EndFrame();
  NoteFramePresented();
  return true;
}

// Beide Renderer melden hier jeden praesentierten Frame: setzt das Flag
// fuer das View-Fokus-Ereignis und misst die Frame-Abstaende.
//
// MESSUNG (Ruckeln): alle 120 gezaehlten Frames eine Zeile. Abstaende ueber
// 500 ms sind Leerlauf (Flutter zeichnet nur bei Bedarf) und fallen aus der
// Wertung - die erste Fassung zaehlte sie mit und meldete 7 fps "im Mittel"
// fuer eine ruhig daliegende App.
void NoteFramePresented() {
  g_first_frame_presented = true;

  static uint64_t last_ns = 0;
  static uint64_t sum_ns = 0;
  static uint64_t worst_ns = 0;
  static int count = 0;
  const uint64_t now_ns = FlutterEngineGetCurrentTime();
  if (last_ns != 0) {
    const uint64_t delta = now_ns - last_ns;
    if (delta > 500'000'000ULL) {
      // Pause: Fenster weiterlaufen lassen, aber den Abstand nicht werten.
    } else {
      sum_ns += delta;
      if (delta > worst_ns) {
        worst_ns = delta;
      }
      if (++count == 120) {
        LOG_INFO("Frames: %.1f/s im Zeichnen, schlechtester Abstand %.0f ms",
                 120.0 * 1e9 / static_cast<double>(sum_ns),
                 static_cast<double>(worst_ns) / 1e6);
        sum_ns = 0;
        worst_ns = 0;
        count = 0;
      }
    }
  }
  last_ns = now_ns;
}

void EngineLog(const char* tag, const char* message, void* user_data) {
  LOG_INFO("[engine:%s] %s", tag != nullptr ? tag : "?",
           message != nullptr ? message : "");
}

const char* ResultName(FlutterEngineResult result) {
  switch (result) {
    case kSuccess:               return "kSuccess";
    case kInvalidLibraryVersion: return "kInvalidLibraryVersion";
    case kInvalidArguments:      return "kInvalidArguments";
    case kInternalInconsistency: return "kInternalInconsistency";
  }
  return "unbekannt";
}

// Berührungen in Flutter-Ereignisse übersetzen.
//
// Der Kern der Aufgabe ist nicht das Umrechnen von Koordinaten, sondern die
// Zustandsverfolgung: Der Touchscreen meldet nur, welche Finger *gerade*
// aufliegen. Flutter erwartet dagegen einen Ablauf aus kDown, beliebig vielen
// kMove und einem kUp. Wer die Momentaufnahme ungefiltert weiterreicht,
// erzeugt endlose kDown-Ereignisse, und keine Schaltfläche reagiert.
//
// Deshalb wird der vorige Zustand behalten und verglichen: neu hinzugekommen
// heißt kDown, weiterhin vorhanden heißt kMove, verschwunden heißt kUp.
struct TrackedTouch {
  int32_t id = 0;
  double x = 0.0;
  double y = 0.0;
  bool in_use = false;
};

TrackedTouch g_previous[flutter_libnx::kMaxTouchPoints];

void SendTouchEvents(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                     const flutter_libnx::SwitchPlatform& platform) {
  if (engine == nullptr) {
    return;
  }

  const flutter_libnx::TouchPoint* current = platform.touches();
  const int count = platform.touch_count();

  FlutterPointerEvent events[flutter_libnx::kMaxTouchPoints * 2] = {};
  size_t event_count = 0;

  // Flutter erwartet Mikrosekunden; FlutterEngineGetCurrentTime liefert
  // Nanosekunden aus derselben Uhr, die die Engine selbst benutzt.
  const size_t now_micros =
      static_cast<size_t>(FlutterEngineGetCurrentTime() / 1000);

  auto fill = [&](FlutterPointerPhase phase, int32_t id, double x, double y) {
    FlutterPointerEvent& e = events[event_count++];
    e.struct_size = sizeof(FlutterPointerEvent);
    e.phase = phase;
    e.timestamp = now_micros;
    e.x = x;
    e.y = y;
    e.device = id;
    e.device_kind = kFlutterPointerDeviceKindTouch;
    // Bei Berührungen gilt der Finger als gedrückte Haupttaste, solange er
    // aufliegt. Ohne das behandelt Flutter kMove als Bewegung ohne Kontakt.
    e.buttons = (phase == kUp) ? 0 : kFlutterPointerButtonMousePrimary;
  };

  // Neue und fortbestehende Berührungen.
  for (int i = 0; i < count; i++) {
    bool was_down = false;
    for (auto& prev : g_previous) {
      if (prev.in_use && prev.id == current[i].id) {
        was_down = true;
        prev.x = current[i].x;
        prev.y = current[i].y;
        break;
      }
    }
    if (was_down) {
      fill(kMove, current[i].id, current[i].x, current[i].y);
    } else {
      for (auto& slot : g_previous) {
        if (!slot.in_use) {
          slot = {current[i].id, current[i].x, current[i].y, true};
          break;
        }
      }
      fill(kDown, current[i].id, current[i].x, current[i].y);
    }
  }

  // Beendete Berührungen: im vorigen Zustand vorhanden, jetzt nicht mehr.
  for (auto& prev : g_previous) {
    if (!prev.in_use) {
      continue;
    }
    bool still_down = false;
    for (int i = 0; i < count; i++) {
      if (current[i].id == prev.id) {
        still_down = true;
        break;
      }
    }
    if (!still_down) {
      fill(kUp, prev.id, prev.x, prev.y);
      prev.in_use = false;
    }
  }

  if (event_count > 0) {
    FlutterEngineSendPointerEvent(engine, events, event_count);
  }
}

// Controller-Eingabe als Tastaturereignisse.
//
// Warum Tasten und kein Mauszeiger: Die Engine meldet sich beim Framework als
// `TargetPlatform.linux` (siehe patch_dart_platform_name). Damit gilt das
// Desktop-Modell – Fokus wandert mit den Pfeiltasten, Aktivieren mit Enter,
// Zurück mit Escape. Genau das leistet Flutters Fokussystem von sich aus, ohne
// dass eine Anwendung etwas dafür tun muss; ein virtueller Zeiger wäre
// zusätzliche Mechanik für dasselbe Ziel.
//
// Die Codes sind nachgeschlagen, nicht geraten: `physical` ist der USB-HID-Wert
// aus `PhysicalKeyboardKey`, `logical` der Wert aus `LogicalKeyboardKey`
// (beide in packages/flutter/lib/src/services/keyboard_key.g.dart).
struct ButtonKeyMap {
  u64 button;        // HidNpadButton_*
  uint64_t physical;
  uint64_t logical;
};

const ButtonKeyMap kButtonKeys[] = {
    {HidNpadButton_Up,    0x00070052, 0x00100000304},  // ArrowUp
    {HidNpadButton_Down,  0x00070051, 0x00100000301},  // ArrowDown
    {HidNpadButton_Left,  0x00070050, 0x00100000302},  // ArrowLeft
    {HidNpadButton_Right, 0x0007004f, 0x00100000303},  // ArrowRight
    // A ist auf der Switch die Bestätigungstaste; sie entspricht Enter.
    {HidNpadButton_A,     0x00070028, 0x0010000000d},  // Enter
    // B ist die Abbruchtaste; Escape ist das, worauf Flutter-Anwendungen
    // für "zurück" hören.
    {HidNpadButton_B,     0x00070029, 0x0010000001b},  // Escape
    // Der linke Stick bewegt den Fokus ebenfalls – bequemer als das
    // Steuerkreuz und auf der Switch die gewohntere Bedienung.
    {HidNpadButton_StickLUp,    0x00070052, 0x00100000304},
    {HidNpadButton_StickLDown,  0x00070051, 0x00100000301},
    {HidNpadButton_StickLLeft,  0x00070050, 0x00100000302},
    {HidNpadButton_StickLRight, 0x0007004f, 0x00100000303},
};

// DIAGNOSE: Die Engine meldet über diesen Rückruf, ob das Ereignis im
// Framework jemanden gefunden hat. Damit trennen sich drei Fälle, die von
// außen gleich aussehen – gar nicht gesendet, gesendet aber nicht zugestellt,
// zugestellt aber von niemandem beansprucht (etwa weil nichts den Fokus hat).
void KeyEventCallback(bool handled, void* user_data) {
  LOG_INFO("Tastenereignis: handled=%d", handled ? 1 : 0);
}

void SendKeyEvents(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                   const flutter_libnx::SwitchPlatform& platform) {
  if (engine == nullptr) {
    return;
  }

  // padGetButtonsDown/-Up liefern die Flanken, nicht den Zustand – genau das,
  // was Flutter erwartet. Dass eine beim Start noch gehaltene Taste nicht als
  // Neudruck erscheint, stellt SwitchPlatform::Initialize sicher, indem es den
  // Vorzustand einliest.
  const u64 down = padGetButtonsDown(&platform.pad());
  const u64 up = padGetButtonsUp(&platform.pad());
  if (down == 0 && up == 0) {
    return;
  }

  const uint64_t now_micros =
      static_cast<uint64_t>(FlutterEngineGetCurrentTime() / 1000);

  for (const ButtonKeyMap& map : kButtonKeys) {
    for (int phase = 0; phase < 2; phase++) {
      const bool is_down = (phase == 0);
      if (((is_down ? down : up) & map.button) == 0) {
        continue;
      }
      FlutterKeyEvent event = {};
      event.struct_size = sizeof(FlutterKeyEvent);
      event.timestamp = static_cast<double>(now_micros);
      event.type = is_down ? kFlutterKeyEventTypeDown : kFlutterKeyEventTypeUp;
      event.physical = map.physical;
      event.logical = map.logical;
      // Kein Zeichen: Pfeiltasten und Enter erzeugen keine Texteingabe.
      event.character = nullptr;
      // `synthesized=true` ist hier kein Etikettenschwindel, sondern der
      // einzige Weg, auf dem ein reiner KeyData-Embedder das Framework
      // erreicht: Nicht-synthetische Ereignisse stellt der KeyEventManager in
      // eine Warteschlange und dispatcht sie erst, wenn die zugehoerige
      // Rohnachricht auf dem Kanal `flutter/keyevent` eintrifft
      // (hardware_keyboard.dart:1088-1113, Modus keyDataThenRawKeyData). Die
      // Desktop-Embedder senden deshalb immer beides als Paar; wir senden
      // keine Rohnachrichten, und die Ereignisse stauten sich fuer immer -
      // Fokus stand, aber jede Taste kam mit handled=0 zurueck.
      //
      // Synthetische Ereignisse bei leerer Warteschlange nehmen die Abkuerzung
      // und werden sofort ueber handleKeyEvent + keyMessageHandler zugestellt
      // (voller Widget-Pfad). Preis: Die handled-Rueckmeldung an uns bleibt
      // immer false - die echte Antwort gibt es nur auf dem
      // Rohnachrichten-Kanal. Wer sie braucht, muss das Nachrichtenpaar der
      // Desktop-Embedder nachbauen (GLFW-Keymap auf `flutter/keyevent`).
      event.synthesized = true;
      event.device_type = kFlutterKeyEventDeviceTypeKeyboard;
      const FlutterEngineResult sent =
          FlutterEngineSendKeyEvent(engine, &event, KeyEventCallback, nullptr);
      LOG_INFO("Taste 0x%llx %s gesendet: %d",
               static_cast<unsigned long long>(map.logical),
               is_down ? "down" : "up", static_cast<int>(sent));
    }
  }
}

// Platform Channels: die erste echte Rückrichtung Dart -> Plattform.
//
// Die Dart-Seite benutzt einen MethodChannel mit JSONMethodCodec - bewusst
// nicht den StandardMethodCodec: Dessen Binärformat nachzubauen wäre eine
// eigene Baustelle, JSON dagegen ist hier eine Zeichenkette mit bekannter
// Form ({"method":"...","args":...}) und die Antwort ein JSON-Array:
// [ergebnis] für Erfolg, [code, meldung, details] für Fehler.
//
// WICHTIG: Sobald ein platform_message_callback registriert ist, beantwortet
// die Engine NICHTS mehr von selbst. Jede Nachricht mit response_handle muss
// beantwortet werden - auch die, die uns nicht interessieren (das Framework
// schickt beim Start z. B. flutter/mousecursor und flutter/textinput). Eine
// leere Antwort heißt für das Framework "nicht implementiert" und löst die
// MissingPluginException aus, auf die gut geschriebener Dart-Code gefasst ist.
// Wer den Handle dagegen liegen lässt, lässt das await auf der Dart-Seite
// für immer hängen.

// Liest den Methodennamen aus {"method":"...",...}. Kein JSON-Parser -
// für das bekannte Format des eigenen Codecs reicht die Suche nach dem
// Schlüssel, und die Nachricht kommt vom eigenen Dart-Code, nicht von außen.
bool JsonMethodIs(const uint8_t* message, size_t size, const char* method) {
  const std::string text(reinterpret_cast<const char*>(message), size);
  const std::string needle = std::string("\"method\":\"") + method + "\"";
  return text.find(needle) != std::string::npos;
}

void PlatformMessageCallback(const FlutterPlatformMessage* message,
                             void* user_data) {
  if (message == nullptr) {
    return;
  }

  auto respond = [&](const char* json) {
    if (message->response_handle == nullptr) {
      return;
    }
    FlutterEngineSendPlatformMessageResponse(
        g_engine, message->response_handle,
        reinterpret_cast<const uint8_t*>(json),
        json != nullptr ? strlen(json) : 0);
  };

  if (strcmp(message->channel, "flutter_libnx/system") == 0) {
    if (JsonMethodIs(message->message, message->message_size,
                     "getBatteryLevel")) {
      u32 charge = 0;
      if (g_psm_ready &&
          R_SUCCEEDED(psmGetBatteryChargePercentage(&charge))) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "[%u]", charge);
        respond(buffer);
      } else {
        respond("[\"unavailable\",\"psm liefert keinen Batteriestand\",null]");
      }
      return;
    }
    // Unbekannte Methode auf unserem Kanal: "nicht implementiert".
    respond(nullptr);
    return;
  }

  // Texteingabe: swkbd-Applet, siehe textinput_horizon.cpp.
  if (flutter_libnx_handle_textinput(g_engine, message)) {
    return;
  }

  // Video: media_kit_video (StandardMethodCodec), siehe
  // media_kit_video_horizon.cpp.
  if (flutter_libnx_handle_media_kit_video(g_engine, message)) {
    return;
  }

  // Die Plugin-Kanäle (StandardMethodCodec) wohnen im Embedder, nicht hier -
  // sie gelten für jede App, nicht nur für dieses Beispiel.
  if (flutter_libnx_handle_plugin_message(g_engine, message)) {
    return;
  }

  // Fremde Kanäle (flutter/textinput, flutter/mousecursor, ...): quittieren,
  // damit kein await hängen bleibt. Siehe Kommentar oben.
  respond(nullptr);
}

}  // namespace

// Diese vier Symbole erzeugt gen_snapshot in app_aot.s. Sie werden fest
// mitgelinkt; die Engine bekommt Zeiger darauf und braucht weder Datei noch
// dynamischen Lader.
//
// gen_snapshot stellt den Namen einen Unterstrich voran - die Konvention von
// Mach-O, die es auch bei ELF-Ausgabe beibehält. Die asm-Bindung stellt den
// Bezug her, ohne dass die Assembly angefasst werden muss.
extern "C" {
extern const uint8_t kDartVmSnapshotData[] asm("_kDartVmSnapshotData");
extern const uint8_t kDartVmSnapshotInstructions[]
    asm("_kDartVmSnapshotInstructions");
extern const uint8_t kDartIsolateSnapshotData[] asm("_kDartIsolateSnapshotData");
extern const uint8_t kDartIsolateSnapshotInstructions[]
    asm("_kDartIsolateSnapshotInstructions");
}

int main(int argc, char* argv[]) {
  flutter_libnx::LogConfig log_config;
  log_config.to_nxlink = false;
  log_config.remote_host = kHostIp;
  log_config.remote_port = kHostPort;
  log_config.file_path = "sdmc:/switch/flutter-libnx/ui_app.log";
  flutter_libnx::LogInit(log_config);

  LOG_INFO("ui_app startet");

  // So frueh wie moeglich, vor der ersten nennenswerten Allokation: geerbte
  // Mappings des Vorlaufs zurueckbauen. Muss vor der Heap-Probe laufen -
  // die fasst bis 256 MB an und koennte sonst selbst in ein Erbstueck laufen.
  flutter_libnx_heal_heap();

  // Ohne diesen Aufruf gibt es kein romfs:/ - und die Engine fände weder
  // Assets noch die ICU-Daten.
  // Heap-Probe, so früh wie möglich.
  //
  // Anlass: Ein Data Abort in `_malloc_r` trat zuletzt auf, bevor die Engine
  // überhaupt startete – zwischen zwei Logzeilen, bei einem einzigen Thread.
  // Damit scheidet Mangel an Speicher für Thread-Stacks aus, und es bleibt die
  // Frage, ob der Heap überhaupt so nutzbar ist, wie er gemeldet wird. Bisher
  // ist das nie geprüft worden – die Zahl `fake_heap_end - fake_heap_start`
  // wurde geglaubt.
  //
  // Geprüft wird in aufsteigenden Größen bis 256 MB: Wenn schon hier etwas
  // fehlschlägt oder abstürzt, liegt die Ursache in der Heap-Übergabe und
  // nicht in Engine oder Framework. Der Speicher wird sofort wieder
  // freigegeben, die Probe kostet also nichts als Zeit.
  {
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    LOG_INFO("Heap-Probe: %p bis %p (%llu MB)",
             static_cast<void*>(fake_heap_start),
             static_cast<void*>(fake_heap_end),
             static_cast<unsigned long long>(
                 (fake_heap_end - fake_heap_start) / (1024 * 1024)));

    size_t last_ok = 0;
    for (size_t mb = 1; mb <= 256; mb *= 4) {
      void* p = std::malloc(mb * 1024 * 1024);
      if (p == nullptr) {
        LOG_ERROR("Heap-Probe: %lu MB fehlgeschlagen (zuletzt %lu MB gut)",
                  static_cast<unsigned long>(mb),
                  static_cast<unsigned long>(last_ok));
        break;
      }
      // Anfang und Ende beschreiben: Eine Reservierung, die sich nicht
      // benutzen lässt, wäre sonst unauffällig.
      static_cast<char*>(p)[0] = 1;
      static_cast<char*>(p)[mb * 1024 * 1024 - 1] = 1;
      std::free(p);
      last_ok = mb;
    }
    LOG_INFO("Heap-Probe: bis %lu MB am Stueck nutzbar",
             static_cast<unsigned long>(last_ok));
  }

  // Baseline: Auffaelligkeiten hier stammen von statischen Konstruktoren
  // oder dem Loader - beides laeuft vor main().
  flutter_libnx_scan_heap("Start");

  // media_kit legt eine Querverweis-Datei unter /tmp ab
  // (NativeReferenceHolder); ohne das Verzeichnis scheitert deren flush
  // mit EBADF-Rauschen im Log. Auf sdmc ist /tmp einfach ein Ordner.
  mkdir("/tmp", 0755);
  // Altlasten zwingend wegraeumen: media_kit PARST diese Dateien als
  // Zeiger. Eine leere/verwaiste Datei aus einem frueheren Lauf liefert
  // Muellzeiger (FormatException, dann Instruction Abort) - und anders
  // als auf Linux ueberlebt /tmp auf der SD-Karte jeden Neustart.
  //
  // KORRIGIERT (2026-08-16): Der Name traegt KEINEN fuehrenden Punkt
  // (media_kit native_reference_holder.dart:123 -
  // "com.alexmercerind.media_kit.NativeReferenceHolder.$pid"). Das alte
  // Muster ".com..." hat nie etwas geloescht - und weil die PID des
  // Wirtsprozesses ueber hbmenu-Reloads stabil ist, fand jeder Lauf die
  // Datei des vorigen. Eine leere (Flush im Vorlauf gescheitert) lieferte
  // int.parse("") -> FormatException, player.handle wurde nie fertig, und
  // media_kit_video rief nie VideoOutputManager.Create - das Intro blieb
  // deshalb ohne Player.
  {
    DIR* tmp_dir = opendir("/tmp");
    if (tmp_dir != nullptr) {
      while (dirent* entry = readdir(tmp_dir)) {
        if (strncmp(entry->d_name, "com.alexmercerind.media_kit", 27) == 0) {
          const std::string stale = std::string("/tmp/") + entry->d_name;
          remove(stale.c_str());
          LOG_INFO("Altlast entfernt: %s", stale.c_str());
        }
      }
      closedir(tmp_dir);
    }
  }

  // Bei Bedarf wieder einschalten (sqlite3-Rauchprobe, siehe oben):
  // flutter_libnx_sqlite3_probe();

  const Result romfs_result = romfsInit();
  if (R_FAILED(romfs_result)) {
    LOG_ERROR("romfsInit fehlgeschlagen: 0x%08x", romfs_result);
    flutter_libnx::LogShutdown();
    return 1;
  }
  LOG_INFO("romfs bereit");
  flutter_libnx_scan_heap("nach romfsInit");

  // GPU zuerst: Gelingt der EGL-Aufbau, rendert Skia ueber Mesa/nouveau
  // direkt auf das NWindow - der Software-Framebuffer darf dann gar nicht
  // erst entstehen (beide wollen dasselbe Fenster). Scheitert EGL, laeuft
  // alles wie bisher in Software weiter.
  const bool use_gpu = flutter_libnx_egl_init();

  flutter_libnx::SwitchPlatform platform;
  if (!platform.Initialize(flutter_libnx::kDefaultWidth,
                           flutter_libnx::kDefaultHeight,
                           /*create_framebuffer=*/!use_gpu)) {
    LOG_ERROR("Plattform konnte nicht eingerichtet werden");
    flutter_libnx::LogShutdown();
    return 1;
  }
  g_platform = &platform;
  flutter_libnx_scan_heap(use_gpu ? "nach EGL-Init" : "nach framebufferCreate");

  // Nachweis, dass die Snapshot-Daten den Weg durch Linker und NRO-Verpackung
  // unbeschadet ueberstanden haben. Das Magic stand im aot_poc schon einmal
  // auf dem Pruefstand.
  uint32_t magic = 0;
  std::memcpy(&magic, kDartVmSnapshotData, sizeof(magic));
  LOG_INFO("VM-Snapshot-Magic: 0x%08x (erwartet 0xdcdcf5f5)", magic);

  // Speicherlage vor dem Start der Engine.
  //
  // Anlass: Mit dem Flutter-Framework scheitert `pthread_create` in
  // fml/thread.cc:80. Jeder Engine-Thread will 2 MB Stack, und libnx holt die
  // aus dem Prozess-Heap. Ob dort Platz ist, lässt sich nur hier beantworten -
  // im Applet-Modus steht deutlich weniger zur Verfügung als im Vollmodus.
  u64 total = 0;
  u64 used = 0;
  svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  // Achtung bei der Lesart: "belegt" ist im Wesentlichen die
  // Heap-Reservierung, nicht der Verbrauch. Aussagekräftig ist, wie viel
  // *ausserhalb* des Heaps frei bleibt - daraus entstehen Thread-Stacks und
  // ihre Spiegelabbildungen. Siehe heap_layout_horizon.cpp.
  extern char* fake_heap_start;
  extern char* fake_heap_end;
  extern bool g_heap_from_loader;
  extern bool g_heap_shrunk;
  const uint64_t heap_size =
      static_cast<uint64_t>(fake_heap_end - fake_heap_start);
  LOG_INFO("Prozessspeicher: %llu MB gesamt, davon %llu MB newlib-Heap, "
           "%llu MB fuer Threads und Abbildungen (Heap %s)",
           static_cast<unsigned long long>(total / (1024 * 1024)),
           static_cast<unsigned long long>(heap_size / (1024 * 1024)),
           static_cast<unsigned long long>((total - heap_size) /
                                           (1024 * 1024)),
           g_heap_shrunk ? "vom Loader, dann verkleinert"
                         : (g_heap_from_loader ? "vom Loader vorgegeben"
                                               : "selbst aufgeteilt"));
  LOG_INFO("Applet-Typ: %d (0 = Anwendung, sonst Applet)",
           static_cast<int>(appletGetAppletType()));

  FlutterRendererConfig renderer = {};
  if (use_gpu) {
    renderer.type = kOpenGL;
    renderer.open_gl.struct_size = sizeof(FlutterOpenGLRendererConfig);
    renderer.open_gl.make_current = [](void*) {
      return flutter_libnx_egl_make_current();
    };
    renderer.open_gl.clear_current = [](void*) {
      return flutter_libnx_egl_clear_current();
    };
    renderer.open_gl.present = [](void*) {
      const bool shown = flutter_libnx_egl_present();
      if (shown) {
        NoteFramePresented();
      }
      return shown;
    };
    // 0 = das Standard-Framebuffer der EGL-Oberflaeche.
    renderer.open_gl.fbo_callback = [](void*) -> uint32_t { return 0; };
    renderer.open_gl.gl_proc_resolver = [](void*, const char* name) {
      return flutter_libnx_egl_resolve(name);
    };
    // Externe Texturen (media_kit-Video): Ohne diesen Callback legt die
    // Engine gar keinen ExternalTextureResolver an, und
    // FlutterEngineRegisterExternalTexture schluege fehl.
    renderer.open_gl.gl_external_texture_frame_callback =
        flutter_libnx_media_kit_texture_frame;
  } else {
    renderer.type = kSoftware;
    renderer.software.struct_size = sizeof(FlutterSoftwareRendererConfig);
    renderer.software.surface_present_callback = SoftwarePresent;
  }

  FlutterProjectArgs project = {};
  project.struct_size = sizeof(FlutterProjectArgs);
  // Aus dem RomFS der NRO, nicht von der SD-Karte: Die App bleibt eine
  // einzige Datei, und Assets können nicht zur Programmversion unpassend sein.
  project.assets_path = "romfs:/flutter_assets";
  project.icu_data_path = "romfs:/icudtl.dat";
  project.log_message_callback = EngineLog;
  project.platform_message_callback = PlatformMessageCallback;
  project.vm_snapshot_data = kDartVmSnapshotData;
  project.vm_snapshot_instructions = kDartVmSnapshotInstructions;
  project.isolate_snapshot_data = kDartIsolateSnapshotData;
  project.isolate_snapshot_instructions = kDartIsolateSnapshotInstructions;

  // Ohne dieses Feld bleibt es bei settings.leak_vm = true (common/settings.h
  // :278, ausgewertet in embedder.cc:2079): FlutterEngineShutdown baut die
  // Shell ab, laesst die Dart-VM aber absichtlich stehen. Dann laeuft auch
  // Dart_Cleanup nie - und damit nicht EventHandler::Stop(), dessen
  // Poll-Thread still in poll() auf dem Weckkanal wartet.
  //
  // Auf Linux, Android und iOS ist das folgenlos: Dort beendet exit() den
  // Prozess und das System raeumt auf. Eine Homebrew-NRO wird dagegen geordnet
  // abgebaut, und dabei verschwindet der Socket-Dienst unter dem wartenden
  // Thread weg. Ein wiederverwendbarer Warmstart, den leak_vm erkaufen soll,
  // bringt hier ohnehin nichts - die NRO wird beendet, nicht neu bespielt.
  project.shutdown_dart_vm_when_done = true;

  // Nur der Plattform-Thread gehört uns; UI und Raster bekommen von der
  // Engine eigene Threads.
  //
  // GESCHICHTE: Bis 2026-08-11 lagen alle drei auf diesem einen Thread -
  // die Reaktion auf "pthread_create scheitert mit ENOMEM". Diese Diagnose
  // war falsch (libnx meldet ENOMEM fuer jeden Threadfehler; wirklich war
  // es die Heap-Erbschaft, siehe porting-notes). Die Folge des Zusammen-
  // legens: Widget-Aufbau, Software-Rasterung (die teuerste Stufe bei
  // 720p) und Eingabe standen in EINER Warteschlange mit 4-ms-Schlaftakt -
  // rezkonv lief spuerbar ruckelig. Mit eigenen Engine-Threads laufen
  // UI-Aufbau und Rasterung als Pipeline auf getrennten Kernen.
  //
  // Der Plattform-Runner bleibt unserer: Plattformnachrichten (Kanaele,
  // swkbd) MUESSEN auf dem Thread der Hauptschleife ankommen, und die
  // Schleife pumpt sie ueber RunExpiredTasks.
  static flutter_libnx::TaskRunner task_runner;
  task_runner.BindToCurrentThread();
  const FlutterTaskRunnerDescription runner_description =
      task_runner.GetDescription();

  FlutterCustomTaskRunners custom_runners = {};
  custom_runners.struct_size = sizeof(FlutterCustomTaskRunners);
  custom_runners.platform_task_runner = &runner_description;
  project.custom_task_runners = &custom_runners;

  // CPU-Boost: Anwendungen laufen standardmäßig mit 1020 MHz; der
  // offizielle Boost-Modus hebt die CPU auf 1785 MHz und senkt dafür den
  // GPU-Takt - für reines Software-Rendering der ideale Tausch (+75 %
  // Rasterleistung, die GPU liegt ohnehin brach). Spiele halten den Modus
  // während Ladebildschirmen minutenlang; wir halten ihn für die Laufzeit
  // und geben ihn beim Abgang zurück (der Wirtsprozess vergisst nichts).
  {
    const Result boost = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    LOG_INFO("CPU-Boost (1785 MHz): %s",
             R_SUCCEEDED(boost) ? "aktiv" : "nicht verfuegbar");
  }

  // Batteriedienst für den Platform-Channel-Nachweis. Scheitert das, läuft
  // alles Übrige weiter - der Kanal antwortet dann mit einem Fehler.
  g_psm_ready = R_SUCCEEDED(psmInitialize());
  if (!g_psm_ready) {
    LOG_WARN("psmInitialize fehlgeschlagen - Batteriekanal meldet Fehler");
  }

  FLUTTER_API_SYMBOL(FlutterEngine) engine = nullptr;
  flutter_libnx_scan_heap("vor EngineInitialize");
  LOG_INFO("FlutterEngineInitialize ...");
  FlutterEngineResult result = FlutterEngineInitialize(
      FLUTTER_ENGINE_VERSION, &renderer, &project, nullptr, &engine);
  LOG_INFO("FlutterEngineInitialize = %d (%s)", static_cast<int>(result),
           ResultName(result));
  g_engine = engine;

  if (result == kSuccess) {
    flutter_libnx_scan_heap("vor RunInitialized");
    LOG_INFO("FlutterEngineRunInitialized ...");
    result = FlutterEngineRunInitialized(engine);
    LOG_INFO("FlutterEngineRunInitialized = %d (%s)", static_cast<int>(result),
             ResultName(result));
  }

  if (result == kSuccess) {
    // Ohne Fenstermaße legt die Engine kein Layout an und zeichnet nichts.
    FlutterWindowMetricsEvent metrics = {};
    metrics.struct_size = sizeof(FlutterWindowMetricsEvent);
    metrics.width = flutter_libnx::kDefaultWidth;
    metrics.height = flutter_libnx::kDefaultHeight;
    metrics.pixel_ratio = 1.0;
    const FlutterEngineResult sent =
        FlutterEngineSendWindowMetricsEvent(engine, &metrics);
    LOG_INFO("SendWindowMetricsEvent = %d (%s)", static_cast<int>(sent),
             ResultName(sent));

    // Lebenszyklus melden.
    //
    // Ohne diese Nachricht bleibt die Anwendung aus Sicht des Frameworks in
    // einem Zustand, in dem sie nicht im Vordergrund ist. Der FocusManager
    // richtet seinen Fokus erst ein, wenn er von einem Wechsel erfährt – der
    // Stacktrace beim allerersten Framework-Start kam nicht zufällig aus
    // `FocusManager._respondToLifecycleChange`.
    //
    // Der Kanal erwartet den nackten Namen des Zustands, kein JSON.
    const char kResumed[] = "AppLifecycleState.resumed";
    FlutterPlatformMessage lifecycle = {};
    lifecycle.struct_size = sizeof(FlutterPlatformMessage);
    lifecycle.channel = "flutter/lifecycle";
    lifecycle.message = reinterpret_cast<const uint8_t*>(kResumed);
    lifecycle.message_size = sizeof(kResumed) - 1;
    const FlutterEngineResult life =
        FlutterEngineSendPlatformMessage(engine, &lifecycle);
    LOG_INFO("Lebenszyklus 'resumed' = %d (%s)", static_cast<int>(life),
             ResultName(life));

  }

  // Das View-Fokus-Ereignis wird bewusst NICHT hier gesendet, sondern erst
  // nach dem ersten präsentierten Frame in der Hauptschleife.
  //
  // Der erste Versuch direkt nach RunInitialized kam mit kSuccess zurück und
  // verpuffte trotzdem – alle Tastenereignisse weiter `handled=0`. Der Weg
  // in der Engine erklärt es: Shell::OnPlatformViewSendViewFocusEvent stellt
  // über RunNowOrPostTask zu (shell.cc:2306), und weil Plattform- und
  // UI-Runner hier derselbe Thread sind, läuft das *sofort* – noch bevor die
  // Schleife unten die Startaufgaben des Isolates abgearbeitet hat. Dann
  // greift RuntimeController::SendViewFocusEvent (runtime_controller.cc:216):
  // ohne PlatformConfiguration stilles `return false`, keine Pufferung, und
  // der Embedder-Aufruf meldet trotzdem kSuccess. Der Empfänger ist erst
  // _ViewState aus `runApp()` (widgets/view.dart:241, über
  // WidgetsBindingObserver.didChangeViewFocus).
  //
  // Steht der erste Frame, hat der Widget-Baum sicher gebaut und der
  // Beobachter existiert. Auf einer Konsole gibt es nur eine View, und sie
  // ist immer im Vordergrund – die Meldung bleibt also einmalig gültig.
  bool focus_sent = false;

  LOG_INFO("Hauptschleife laeuft, Plus beendet");
  while (appletMainLoop()) {
    platform.PollEvents();
    if (padGetButtonsDown(&platform.pad()) & HidNpadButton_Plus) {
      break;
    }
    SendTouchEvents(engine, platform);
    SendKeyEvents(engine, platform);

    // Aufgaben der Engine abarbeiten. Ohne das wird nichts gezeichnet und
    // keine Rückmeldung zugestellt – die Engine hat keinen eigenen Thread
    // mehr, auf dem sie das tun könnte.
    task_runner.RunExpiredTasks(engine);

    // Fertige Videoframes an die Engine melden. Muss auf diesem Thread
    // geschehen (MarkExternalTextureFrameAvailable verlangt den
    // Plattform-Thread); der mpv-Render-Thread setzt nur ein Flag.
    flutter_libnx_media_kit_video_pump(engine);

    if (!focus_sent && g_first_frame_presented && engine != nullptr) {
      FlutterViewFocusEvent focus = {};
      focus.struct_size = sizeof(FlutterViewFocusEvent);
      focus.view_id = 0;  // implicit view
      focus.state = kFocused;
      // kForward statt kUndefined: Damit sucht _ViewState per
      // findFirstFocus das erste fokussierbare Widget – deterministisch,
      // auch falls die autofocus-Anforderung ohne fokussierte View
      // liegengeblieben sein sollte.
      focus.direction = kForward;
      const FlutterEngineResult focused =
          FlutterEngineSendViewFocusEvent(engine, &focus);
      LOG_INFO("View-Fokus nach erstem Frame = %d (%s)",
               static_cast<int>(focused), ResultName(focused));
      focus_sent = true;
    }

    svcSleepThread(4'000'000);  // 4 ms; die Schleife trägt jetzt auch die Engine
  }

  if (engine != nullptr) {
    LOG_INFO("Shutdown ...");
    // Fährt dank shutdown_dart_vm_when_done auch die Dart-VM herunter und
    // sammelt dabei den Poll-Thread des Eventhandlers ein.
    FlutterEngineShutdown(engine);
    LOG_INFO("FlutterEngineShutdown zurueckgekehrt");
  }

  g_platform = nullptr;

  // Erst den mpv-Render-Thread einsammeln (er haelt den geteilten
  // EGL-Kontext gebunden und gibt die mpv-Render-Kontexte frei), dann darf
  // EGL herunterfahren.
  flutter_libnx_media_kit_video_shutdown();

  // Sessions schliessen, die den NRO-Wechsel sonst ueberleben wuerden.
  //
  // Der Wirtsprozess traegt alles nach, was hier liegen bleibt: Nach drei
  // Zyklen mit je einer geleakten pl:u-Session war das Session-Limit des
  // Dienstes erschoepft, und der naechste Verbindungsaufbau im Prozess
  // (hbmenu-Reload) starb mit 2011-0102 (HIPC, "out of sessions").
  // romfs haengt an einem Storage-Handle mit derselben Lebensdauer.
  flutter_libnx_egl_shutdown();
  appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  flutter_libnx_fonts_cleanup();
  flutter_libnx_random_cleanup();
  if (g_psm_ready) {
    psmExit();
    g_psm_ready = false;
  }
  romfsExit();

  // Die Senke bleibt bis zuletzt offen: Wird sie vor dem Abbau geschlossen,
  // waere jede Meldung aus der Abbauphase unsichtbar - genau der Fehler, der
  // bei dieser Fehlersuche dreimal Zeit gekostet hat.
  LOG_INFO("ui_app beendet");
  flutter_libnx::LogShutdown();
  return 0;
}
