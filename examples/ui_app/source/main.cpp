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

#include <cstdio>
#include <cstring>

#include "embedder.h"

#include "flutter_libnx/log.h"
#include "flutter_libnx/switch_platform.h"

namespace {

constexpr const char* kHostIp = "192.168.0.153";
constexpr uint16_t kHostPort = 28800;

flutter_libnx::SwitchPlatform* g_platform = nullptr;

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
  return true;
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

  // Ohne diesen Aufruf gibt es kein romfs:/ - und die Engine fände weder
  // Assets noch die ICU-Daten.
  const Result romfs_result = romfsInit();
  if (R_FAILED(romfs_result)) {
    LOG_ERROR("romfsInit fehlgeschlagen: 0x%08x", romfs_result);
    flutter_libnx::LogShutdown();
    return 1;
  }
  LOG_INFO("romfs bereit");

  flutter_libnx::SwitchPlatform platform;
  if (!platform.Initialize()) {
    LOG_ERROR("Framebuffer konnte nicht eingerichtet werden");
    flutter_libnx::LogShutdown();
    return 1;
  }
  g_platform = &platform;

  // Nachweis, dass die Snapshot-Daten den Weg durch Linker und NRO-Verpackung
  // unbeschadet ueberstanden haben. Das Magic stand im aot_poc schon einmal
  // auf dem Pruefstand.
  uint32_t magic = 0;
  std::memcpy(&magic, kDartVmSnapshotData, sizeof(magic));
  LOG_INFO("VM-Snapshot-Magic: 0x%08x (erwartet 0xdcdcf5f5)", magic);

  FlutterRendererConfig renderer = {};
  renderer.type = kSoftware;
  renderer.software.struct_size = sizeof(FlutterSoftwareRendererConfig);
  renderer.software.surface_present_callback = SoftwarePresent;

  FlutterProjectArgs project = {};
  project.struct_size = sizeof(FlutterProjectArgs);
  // Aus dem RomFS der NRO, nicht von der SD-Karte: Die App bleibt eine
  // einzige Datei, und Assets können nicht zur Programmversion unpassend sein.
  project.assets_path = "romfs:/flutter_assets";
  project.icu_data_path = "romfs:/icudtl.dat";
  project.log_message_callback = EngineLog;
  project.vm_snapshot_data = kDartVmSnapshotData;
  project.vm_snapshot_instructions = kDartVmSnapshotInstructions;
  project.isolate_snapshot_data = kDartIsolateSnapshotData;
  project.isolate_snapshot_instructions = kDartIsolateSnapshotInstructions;

  FLUTTER_API_SYMBOL(FlutterEngine) engine = nullptr;
  LOG_INFO("FlutterEngineInitialize ...");
  FlutterEngineResult result = FlutterEngineInitialize(
      FLUTTER_ENGINE_VERSION, &renderer, &project, nullptr, &engine);
  LOG_INFO("FlutterEngineInitialize = %d (%s)", static_cast<int>(result),
           ResultName(result));

  if (result == kSuccess) {
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
  }

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);

  LOG_INFO("Hauptschleife laeuft, Plus beendet");
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
      break;
    }
    svcSleepThread(16'000'000);  // rund 60 Hz, ohne den Kern auszulasten
  }

  if (engine != nullptr) {
    LOG_INFO("Shutdown ...");
    FlutterEngineShutdown(engine);
  }
  g_platform = nullptr;
  LOG_INFO("ui_app beendet");
  flutter_libnx::LogShutdown();
  return 0;
}
