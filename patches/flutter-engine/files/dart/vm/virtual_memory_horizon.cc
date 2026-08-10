// Copyright 2026 The flutter-libnx Authors.
//
// VirtualMemory für Horizon OS.
//
// Horizon bietet kein mmap. libnx kennt zwar `virtmem` für die Verwaltung des
// Adressraums und `svcMapMemory` für das eigentliche Abbilden, aber für den
// ersten lauffähigen Port ist das mehr Maschinerie als nötig: Die Dart-VM
// braucht im AOT-Product-Modus ausschließlich lesbar-schreibbaren Speicher.
//
// Deshalb liegt hier die einfachste Lösung, die die Zusicherungen erfüllt:
// ausgerichtete Anforderungen über `memalign` aus dem Prozess-Heap.
//
// Was das kostet und was nicht:
//
//   * Reserve/Commit fallen zusammen. Angeforderter Speicher wird sofort
//     belegt, nicht nur der Adressraum reserviert. Für den Dart-Heap ist das
//     unkritisch, weil er ohnehin wächst; für die 4-GB-Reservierung der
//     Compressed Pointers wäre es tödlich – siehe unten.
//   * `Protect` ist wirkungslos. Im AOT-Product-Modus ist das vertretbar:
//     Ausführbarer Speicher wird nicht gebraucht (die Snapshot-Instruktionen
//     liegen in der .text der NRO), und der Schreibschutz von Code-Seiten
//     entfällt mangels zur Laufzeit erzeugtem Code.
//   * `FreeSubSegment` kann nicht erfüllt werden. Ein `memalign`-Block lässt
//     sich nicht teilweise zurückgeben; die Funktion meldet das ehrlich mit
//     `false`, worauf `Truncate` lediglich seine Buchführung anpasst.
//
// Compressed Pointers sind mit dieser Fassung unvereinbar: `VirtualMemory::Init`
// reserviert dafür 4 GB, ausgerichtet auf 4 GB, und bricht sonst mit FATAL ab
// (vgl. virtual_memory_posix.cc). Vier Gigabyte tatsächlich zu belegen ist auf
// einer Konsole mit 4 GB RAM aussichtslos. Der Build setzt deshalb
// `dart_use_compressed_pointers=false`. Der ASSERT unten stellt sicher, dass
// diese Verbindung nicht unbemerkt zerfällt.

#include "vm/virtual_memory.h"

#include <malloc.h>

#include "platform/assert.h"
#include "platform/utils.h"
#include "vm/memory_region.h"

#if defined(DART_COMPRESSED_POINTERS)
#error Compressed Pointers verlangen eine 4-GB-Adressraumreservierung, die \
diese Implementierung nicht leisten kann. Mit dart_use_compressed_pointers=false \
bauen oder Reserve/Commit ueber libnx virtmem nachruesten.
#endif

namespace dart {

uword VirtualMemory::page_size_ = 0;
VirtualMemory* VirtualMemory::compressed_heap_ = nullptr;

void VirtualMemory::Init() {
  // Horizon verwendet durchgängig 4-KB-Seiten. Es gibt keine Abfrage zur
  // Laufzeit, die etwas anderes liefern könnte.
  page_size_ = 4096;
}

void VirtualMemory::Cleanup() {
  page_size_ = 0;
}

VirtualMemory* VirtualMemory::AllocateAligned(intptr_t size,
                                              intptr_t alignment,
                                              bool is_executable,
                                              bool is_compressed,
                                              const char* name) {
  ASSERT(Utils::IsAligned(size, PageSize()));
  ASSERT(Utils::IsPowerOfTwo(alignment));
  ASSERT(Utils::IsAligned(alignment, PageSize()));
  ASSERT(name != nullptr);

  // Ausführbaren Speicher gibt es hier nicht. Im AOT-Product-Modus fordert die
  // VM ihn nicht an; sollte es doch geschehen, ist das ein Fehler, den wir
  // sehen wollen, statt ihn zu verschleiern.
  ASSERT(!is_executable);
  ASSERT(!is_compressed);

  void* address = memalign(alignment, size);
  if (address == nullptr) {
    return nullptr;
  }

  MemoryRegion region(address, size);
  return new VirtualMemory(region, region);
}

VirtualMemory::~VirtualMemory() {
  if (vm_owns_region()) {
    free(reserved_.pointer());
  }
}

bool VirtualMemory::FreeSubSegment(void* address, intptr_t size) {
  // Ein memalign-Block ist unteilbar. Truncate() wertet das Ergebnis aus und
  // passt in diesem Fall nur seine Buchführung an.
  return false;
}

void VirtualMemory::DontNeed(void* address, intptr_t size) {
  // Unter POSIX gibt madvise(MADV_DONTNEED) Seiten an das System zurueck, die
  // spaeter neu eingelagert werden koennen. Ein Heap-Puffer kann das nicht -
  // ein Verwerfen wuerde die Daten verlieren. Deshalb eine Leeroperation,
  // passend zu IsDontNeedSafe() == false in fml/mapping_horizon.cc.
}

void VirtualMemory::Protect(void* address, intptr_t size, Protection mode) {
  // Ohne mprotect-Entsprechung wirkungslos. Der Aufrufer verlässt sich im
  // AOT-Product-Modus nicht darauf: Schreibschutz für Code-Seiten betrifft nur
  // zur Laufzeit erzeugten Code, und den gibt es hier nicht.
}

}  // namespace dart
