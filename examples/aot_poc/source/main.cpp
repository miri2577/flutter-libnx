// Meilenstein 1b: Beweist, dass ein Dart-AOT-Snapshot als Assembly in eine NRO
// gelinkt und zur Laufzeit über Symbolreferenzen erreicht werden kann.
//
// Was hier NICHT passiert: Der Snapshot wird nicht ausgeführt. Dafür fehlt die
// Dart-VM, die es für Horizon noch nicht gibt. Geprüft wird ausschließlich der
// Ladeweg – und genau der war die offene Kernfrage des Projekts.

#include <switch.h>

#include <cstdint>
#include <cstdio>

#include "flutter_libnx/log.h"

// Entwicklungsrechner, an den die Logausgabe geschickt wird. Die Switch baut die
// Verbindung auf, deshalb ist NAT auf der Gegenseite unerheblich.
namespace {
constexpr const char* kHostIp = "192.168.0.153";
constexpr uint16_t kHostPort = 28800;
}  // namespace

// Von gen_snapshot erzeugt. Die Namen tragen einen führenden Unterstrich,
// weil gen_snapshot die Mach-O-Konvention verwendet; die Flutter Engine sucht
// über dlsym dagegen nach den Namen ohne Unterstrich. Für uns spielt das keine
// Rolle: Wir übergeben der Embedder-API später Zeiger, keine Namen.
extern "C" {
extern const uint8_t _kDartVmSnapshotData[];
extern const uint8_t _kDartVmSnapshotInstructions[];
extern const uint8_t _kDartIsolateSnapshotData[];
extern const uint8_t _kDartIsolateSnapshotInstructions[];
}

namespace {

void PrintSymbol(const char* name, const uint8_t* addr) {
  char bytes[64];
  int offset = 0;
  for (int i = 0; i < 8; ++i) {
    offset += std::snprintf(bytes + offset, sizeof(bytes) - offset, " %02x", addr[i]);
  }

  std::printf("  %-32s %p\n", name, static_cast<const void*>(addr));
  std::printf("      erste Bytes:%s\n", bytes);

  // Dieselbe Information geht an den Entwicklungsrechner, damit sie nicht vom
  // Bildschirm abgeschrieben werden muss.
  LOG_INFO("%-32s %p  erste Bytes:%s", name, static_cast<const void*>(addr), bytes);
}

}  // namespace

int main(int argc, char* argv[]) {
  consoleInit(nullptr);

  flutter_libnx::LogConfig log_config;
  log_config.to_nxlink = false;  // Konsole und nxlink teilen sich stdout.
  log_config.remote_host = kHostIp;
  log_config.remote_port = kHostPort;
  log_config.file_path = "sdmc:/switch/flutter-libnx/aot_poc.log";
  flutter_libnx::LogInit(log_config);

  std::printf("\n  flutter-libnx / AOT-Ladeweg-PoC\n");
  std::printf("  ==============================\n\n");
  std::printf("  Logempfaenger %s:%u -> %s\n\n", kHostIp, kHostPort,
              flutter_libnx::LogHasRemote() ? "verbunden" : "nicht erreichbar");

  PrintSymbol("kDartVmSnapshotData", _kDartVmSnapshotData);
  PrintSymbol("kDartVmSnapshotInstructions", _kDartVmSnapshotInstructions);
  PrintSymbol("kDartIsolateSnapshotData", _kDartIsolateSnapshotData);
  PrintSymbol("kDartIsolateSnapshotInstructions", _kDartIsolateSnapshotInstructions);

  // Die Instruktionen liegen laut nm in .text. Wenn die NRO korrekt geladen
  // wurde, muss dieser Bereich ausführbar gemappt sein – wir lesen ihn hier nur,
  // ausgeführt wird nichts.
  LOG_INFO("Adresse von main(): %p", reinterpret_cast<void*>(&main));
  std::printf("\n  Adresse von main(): %p\n", reinterpret_cast<void*>(&main));
  std::printf("  -> Instruktionen und Code liegen im selben Modul, wenn die\n");
  std::printf("     Adressen dieselbe Groessenordnung haben.\n");

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

  LOG_INFO("PoC beendet");
  flutter_libnx::LogShutdown();
  consoleExit(nullptr);
  return 0;
}
