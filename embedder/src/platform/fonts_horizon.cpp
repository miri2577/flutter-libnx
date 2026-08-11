// Systemschriften der Switch für die Engine bereitstellen.
//
// Skia bekommt seinen Schriftmanager über eine Plattformdatei in `txt`. Für
// Horizon griff bisher der generische Rückfall (`txt/src/txt/platform.cc:19`)
// mit `SkFontMgr_New_Custom_Empty()` – ein Manager ohne eine einzige Schrift.
// Rechtecke wurden gezeichnet, Text nicht.
//
// Die Konsole bringt eigene Schriften mit, erreichbar über den Dienst `pl:u`.
// Sie liegen in einem gemeinsamen Speicherbereich; `plGetSharedFontByType`
// liefert nur Adresse und Größe, ohne zu kopieren. Deshalb bleiben die Zeiger
// über die Laufzeit gültig, und die Engine kann sie ohne eigene Kopie
// benutzen.
//
// Warum eine eigene Datei: `<switch.h>` bringt Makros und Typnamen mit, die
// sich mit Skia- und Dart-Bezeichnern beißen. Dieselbe Trennung wie bei
// `random_horizon.cpp` und `stack_bounds_horizon.cpp`.

#include <switch.h>

#include <stddef.h>
#include <stdint.h>

#include "flutter_libnx/log.h"

namespace {

// Reihenfolge ist Absicht: Die Standardschrift zuerst, damit sie zur
// Vorgabefamilie wird. Danach die Sprachschnitte, damit Text außerhalb von
// Latin nicht als Leerkästchen erscheint. Die Nintendo-Sonderzeichen (Tasten-
// symbole) kommen zuletzt – sie ergänzen, sie ersetzen nichts.
const PlSharedFontType kFontOrder[] = {
    PlSharedFontType_Standard,
    PlSharedFontType_ChineseSimplified,
    PlSharedFontType_ExtChineseSimplified,
    PlSharedFontType_ChineseTraditional,
    PlSharedFontType_KO,
    PlSharedFontType_NintendoExt,
};

constexpr int kFontCount =
    static_cast<int>(sizeof(kFontOrder) / sizeof(kFontOrder[0]));

bool g_initialized = false;
bool g_available = false;

// Einmalig den Dienst öffnen. Schlägt das fehl, bleibt es beim leeren
// Schriftmanager – die App zeichnet dann wie bisher, nur ohne Text.
void EnsureInitialized() {
  if (g_initialized) {
    return;
  }
  g_initialized = true;

  const Result rc = plInitialize(PlServiceType_User);
  if (R_FAILED(rc)) {
    LOG_ERROR("plInitialize fehlgeschlagen: 0x%08x - kein Systemtext", rc);
    return;
  }
  g_available = true;
}

}  // namespace

extern "C" {

// Liefert die Systemschrift mit laufender Nummer. Rückgabe false heißt
// „keine weitere" und beendet damit die Schleife auf der Engine-Seite.
bool flutter_libnx_get_system_font(int index,
                                   const void** data,
                                   size_t* size) {
  if ((data == nullptr) || (size == nullptr)) {
    return false;
  }
  if ((index < 0) || (index >= kFontCount)) {
    return false;
  }

  EnsureInitialized();
  if (!g_available) {
    return false;
  }

  PlFontData font = {};
  const Result rc = plGetSharedFontByType(&font, kFontOrder[index]);
  if (R_FAILED(rc)) {
    LOG_WARN("plGetSharedFontByType(%d) fehlgeschlagen: 0x%08x", index, rc);
    return false;
  }
  if ((font.address == nullptr) || (font.size == 0)) {
    LOG_WARN("Schrift %d ist leer", index);
    return false;
  }

  // Die Signatur bleibt im Protokoll, in einer Zeile je Schrift. Sie hat die
  // Annahme widerlegt, Nintendo liefere die Schriften im BFTTF-Format: Dort
  // steht 00 01 00 00, also rohes TrueType, und eine Umwandlung hätte die
  // Daten zerstört. Wer das später anzweifelt, sieht es sofort.
  const uint8_t* head = static_cast<const uint8_t*>(font.address);
  LOG_INFO("Schrift %d: %u Bytes, Signatur %02x %02x %02x %02x", index,
           static_cast<unsigned>(font.size), head[0], head[1], head[2],
           head[3]);

  *data = font.address;
  *size = static_cast<size_t>(font.size);
  return true;
}

// Den Schriftdienst wieder schliessen.
//
// Anlass: Systemabsturz 2011-0102 (HIPC, "out of sessions") beim jeweils
// dritten NRO-Zyklus im selben Wirtsprozess - erst nach dem sauberen Abgang
// der App, beim Teardown danach. Sessions ueberleben den NRO-Wechsel genau
// wie die Heap-Zustaende: Jeder Lauf liess hier eine pl:u-Session (samt
// gemappter Shared-Font-Memory) zurueck, bis das Limit des Dienstes erreicht
// war und der naechste Verbindungsaufbau im Prozess toedlich scheiterte.
//
// Voraussetzung des Aufrufers: Die Engine ist bereits abgebaut - die
// Schriftdaten zeigen in die Shared Memory, die plExit unmappt.
void flutter_libnx_fonts_cleanup(void) {
  if (g_available) {
    plExit();
  }
  g_available = false;
  g_initialized = false;
}

}  // extern "C"
