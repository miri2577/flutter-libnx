// Copyright 2026 The flutter-libnx Authors.
//
// Schriftmanager für Horizon OS.
//
// Ohne diese Datei greift der generische Rückfall `txt/src/txt/platform.cc`
// mit `SkFontMgr_New_Custom_Empty()` – ein Manager ohne eine einzige Schrift.
// Auf Hardware sah das so aus: Rechtecke wurden gezeichnet, Text nicht.
//
// Die Konsole bringt eigene Schriften mit (Dienst `pl:u`). Sie liegen in
// einem gemeinsamen Speicherbereich und bleiben über die Laufzeit gültig,
// weshalb sie ohne Kopie durchgereicht werden können.
//
// Der Zugriff selbst liegt im Embedder (`fonts_horizon.cpp`), weil
// `<switch.h>` hier nicht hinein darf – dieselbe Trennung wie bei den
// Stackgrenzen der Dart-VM und der Logsenke.

#include "txt/platform.h"

#include <vector>

#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkSpan.h"
#include "third_party/skia/include/ports/SkFontMgr_data.h"
#include "third_party/skia/include/ports/SkFontMgr_empty.h"

// Vom Embedder gestellt. Fehlt er – etwa in einem Testprogramm, das nur die
// Engine linkt –, ist der Zeiger null und es bleibt beim leeren Manager.
extern "C" __attribute__((weak)) bool flutter_libnx_get_system_font(
    int index,
    const void** data,
    size_t* size);

// Dieselbe Senke wie OS::Print und Syslog. Fehlt der Embedder, ist der Zeiger
// null und es bleibt still.
extern "C" __attribute__((weak)) void flutter_libnx_vm_log(const char* text);

namespace txt {

std::vector<std::string> GetDefaultFontFamilies() {
  // Der Name der Standardschrift der Konsole. Findet Skia ihn nicht, greift
  // die Vorgabefamilie des Managers – das ist die zuerst eingetragene, und
  // der Embedder trägt die Standardschrift zuerst ein.
  return {"Nintendo Standard"};
}

sk_sp<SkFontMgr> GetDefaultFontManager(uint32_t font_initialization_data) {
  static sk_sp<SkFontMgr> mgr = [] {
    std::vector<sk_sp<SkData>> fonts;

    if (flutter_libnx_get_system_font != nullptr) {
      const void* data = nullptr;
      size_t size = 0;
      for (int index = 0;
           flutter_libnx_get_system_font(index, &data, &size); index++) {
        // Ohne Kopie: Der Speicher gehört dem System und bleibt gültig,
        // solange der Dienst offen ist – und der wird nicht geschlossen.
        fonts.push_back(SkData::MakeWithoutCopy(data, size));
      }
    }

    if (fonts.empty()) {
      // Ehrlicher Rückfall statt stiller Halbheit: Ohne Schriften gibt es
      // keinen Text, und das soll dieselbe sichtbare Ursache haben wie zuvor.
      // Bleibt als Meldung: Ohne Schriften gibt es keinen Text, und das soll
      // eine sichtbare Ursache haben statt ein leeres Bild.
      if (flutter_libnx_vm_log != nullptr) {
        flutter_libnx_vm_log(
            "txt: keine Systemschriften erhalten, Text bleibt leer\n");
      }
      return SkFontMgr_New_Custom_Empty();
    }
    return SkFontMgr_New_Custom_Data(SkSpan(fonts.data(), fonts.size()));
  }();
  return mgr;
}

}  // namespace txt
