// Meilenstein 1b: Beweist, dass ein Dart-AOT-Snapshot als Assembly in eine NRO
// gelinkt und zur Laufzeit über Symbolreferenzen erreicht werden kann.
//
// Was hier NICHT passiert: Der Snapshot wird nicht ausgeführt. Dafür fehlt die
// Dart-VM, die es für Horizon noch nicht gibt. Geprüft wird ausschließlich der
// Ladeweg – und genau der war die offene Kernfrage des Projekts.

#include <switch.h>

#include <cstdint>
#include <cstdio>

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
  std::printf("  %-32s %p\n", name, static_cast<const void*>(addr));
  std::printf("      erste Bytes:");
  for (int i = 0; i < 8; ++i) {
    std::printf(" %02x", addr[i]);
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char* argv[]) {
  consoleInit(nullptr);

  std::printf("\n  flutter-libnx / AOT-Ladeweg-PoC\n");
  std::printf("  ==============================\n\n");

  PrintSymbol("kDartVmSnapshotData", _kDartVmSnapshotData);
  PrintSymbol("kDartVmSnapshotInstructions", _kDartVmSnapshotInstructions);
  PrintSymbol("kDartIsolateSnapshotData", _kDartIsolateSnapshotData);
  PrintSymbol("kDartIsolateSnapshotInstructions", _kDartIsolateSnapshotInstructions);

  // Die Instruktionen liegen laut nm in .text. Wenn die NRO korrekt geladen
  // wurde, muss dieser Bereich ausführbar gemappt sein – wir lesen ihn hier nur,
  // ausgeführt wird nichts.
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

  consoleExit(nullptr);
  return 0;
}
