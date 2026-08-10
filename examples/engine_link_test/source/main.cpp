// Linktest: Bindet die für Horizon gebaute Flutter Engine in eine NRO.
//
// Das Programm startet die Engine nicht – es ruft nur Funktionen auf, die
// ohne Initialisierung auskommen. Zweck ist ausschließlich der Nachweis, dass
// libflutter_engine.a, libnx und devkitA64 zu einem lauffähigen Programm
// zusammenfinden.
//
// Genau das zeigt der Compiler nicht: Übersetzen heißt, dass jede Datei für
// sich stimmig ist. Erst der Linker prüft, ob alle Symbole zueinander passen.

#include <switch.h>

#include <cstdint>
#include <cstdio>

// Der öffentliche Header der Embedder-API. Im Engine-Baum heißt er embedder.h;
// erst beim Ausliefern wird er zu flutter_embedder.h kopiert.
#include "embedder.h"

#include "flutter_libnx/log.h"

namespace {

constexpr const char* kHostIp = "192.168.0.153";
constexpr uint16_t kHostPort = 28800;

}  // namespace

int main(int argc, char* argv[]) {
  consoleInit(nullptr);

  flutter_libnx::LogConfig log_config;
  log_config.to_nxlink = false;
  log_config.remote_host = kHostIp;
  log_config.remote_port = kHostPort;
  log_config.file_path = "sdmc:/switch/flutter-libnx/engine_link_test.log";
  flutter_libnx::LogInit(log_config);

  std::printf("\n  flutter-libnx / Engine-Linktest\n");
  std::printf("  ==============================\n\n");

  // Erste echte Auskunft der Engine auf Switch-Hardware.
  const bool aot = FlutterEngineRunsAOTCompiledDartCode();
  std::printf("  FlutterEngineRunsAOTCompiledDartCode: %s\n",
              aot ? "true" : "false");
  LOG_INFO("FlutterEngineRunsAOTCompiledDartCode = %d", aot ? 1 : 0);

  // Die Struktur der Embedder-API zur Laufzeit gegenprüfen: struct_size ist
  // das Feld, über das die Engine Versionsunterschiede erkennt.
  FlutterProjectArgs args = {};
  args.struct_size = sizeof(FlutterProjectArgs);
  std::printf("  sizeof(FlutterProjectArgs)          : %zu\n",
              args.struct_size);
  LOG_INFO("sizeof(FlutterProjectArgs) = %zu", args.struct_size);

  FlutterRendererConfig config = {};
  config.type = kSoftware;
  std::printf("  sizeof(FlutterRendererConfig)       : %zu\n",
              sizeof(FlutterRendererConfig));
  std::printf("  Renderer-Typ kSoftware              : %d\n",
              static_cast<int>(config.type));

  // Ein Aufruf, der tatsächlich in die Engine springt: die monotone Uhr.
  const uint64_t t0 = FlutterEngineGetCurrentTime();
  std::printf("  FlutterEngineGetCurrentTime         : %llu ns\n",
              static_cast<unsigned long long>(t0));
  LOG_INFO("FlutterEngineGetCurrentTime = %llu",
           static_cast<unsigned long long>(t0));

  std::printf("\n  Plus zum Beenden.\n");
  consoleUpdate(nullptr);

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  padUpdate(&pad);

  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
      break;
    }
    consoleUpdate(nullptr);
  }

  LOG_INFO("Linktest beendet");
  flutter_libnx::LogShutdown();
  consoleExit(nullptr);
  return 0;
}
