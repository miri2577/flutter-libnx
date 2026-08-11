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

// Der Wächter ist nicht bloß Formsache: Die Datei steht unbedingt in der
// Quellenliste, und beim Bau von gen_snapshot für den Entwicklungsrechner ist
// der Host Linux. Ohne ihn wären hier und in virtual_memory_posix.cc dieselben
// Symbole definiert. Die vier übrigen Horizon-Quellen tragen ihn längst; diese
// war die einzige ohne, und es fiel erst auf, als zum ersten Mal etwas für den
// Host gebaut wurde.
#include "platform/globals.h"

#if defined(DART_HOST_OS_HORIZON)

#include "vm/virtual_memory.h"

#include <malloc.h>

#include "platform/assert.h"
#include "platform/utils.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/memory_region.h"

#if defined(DART_COMPRESSED_POINTERS)
#error Compressed Pointers verlangen eine 4-GB-Adressraumreservierung, die \
diese Implementierung nicht leisten kann. Mit dart_use_compressed_pointers=false \
bauen oder Reserve/Commit ueber libnx virtmem nachruesten.
#endif

// DEBUG-INSTRUMENTIERUNG (flutter-libnx): siehe os_horizon.cc. Der Embedder
// stellt diese Funktion bereit; fehlt sie, ist der Zeiger null.
extern "C" __attribute__((weak)) void flutter_libnx_vm_log(const char* text);

namespace {
void VmLog(const char* format, ...) {
  if (flutter_libnx_vm_log == nullptr) {
    return;
  }
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  flutter_libnx_vm_log(buffer);
}
}  // namespace

// WÄCHTER-BYTES (flutter-libnx, Fehlersuche zum Absturz in _malloc_r).
//
// Ein Data Abort in newlibs malloc bedeutet beschädigte Verwaltungsstrukturen:
// Irgendwer schreibt über einen Block hinaus. Der Schaden entsteht früher als
// der Absturz, deshalb nützt die Absturzstelle wenig.
//
// Hier bekommt jeder Block der VM einen Wächterbereich hinter dem Nutzteil.
// Wird er beim Freigeben verändert vorgefunden, ist der Überlauf genau diesem
// Block zuzuordnen – mit Name und Größe. Zusätzlich werden in Abständen alle
// lebenden Blöcke geprüft, damit sich der Schaden zeitlich eingrenzen lässt,
// statt erst beim Freigeben aufzufallen.
//
// Bewusst nur *hinter* dem Nutzteil: Ein Wächter davor würde die Ausrichtung
// verschieben, und genau die verlangt die VM (bis zu 512 KB).
//
// Nach der Fehlersuche wieder entfernen - die Prüfung kostet bei jeder
// Allokation Zeit.
namespace {

constexpr intptr_t kGuardBytes = 64;
constexpr uint8_t kGuardPattern = 0xA5;

// Wie oft alle lebenden Blöcke geprüft werden. Bei jeder Allokation wäre es
// gründlicher, aber die VM allokiert in Schüben - das würde den Start spürbar
// bremsen.
constexpr intptr_t kSweepEvery = 64;

struct LiveBlock {
  uint8_t* address;
  intptr_t size;
  const char* name;
  LiveBlock* next;
};

LiveBlock* g_live = nullptr;
intptr_t g_alloc_count = 0;
pthread_mutex_t g_guard_lock = PTHREAD_MUTEX_INITIALIZER;

bool GuardIntact(const uint8_t* address, intptr_t size) {
  for (intptr_t i = 0; i < kGuardBytes; i++) {
    if (address[size + i] != kGuardPattern) {
      return false;
    }
  }
  return true;
}

void ReportBroken(const LiveBlock& block) {
  VmLog("HEAP-UEBERLAUF: Block '%s' (%ld Bytes, %p) hat seinen Waechter "
        "ueberschrieben",
        block.name != nullptr ? block.name : "?",
        static_cast<long>(block.size), block.address);
  // Die ersten Bytes des Wächters zeigen oft, was hineingeschrieben wurde.
  const uint8_t* g = block.address + block.size;
  VmLog("  Waechter: %02x %02x %02x %02x %02x %02x %02x %02x", g[0], g[1],
        g[2], g[3], g[4], g[5], g[6], g[7]);
}

// Alle lebenden Blöcke prüfen. Der Aufrufer hält die Sperre.
void SweepLocked() {
  for (LiveBlock* b = g_live; b != nullptr; b = b->next) {
    if (!GuardIntact(b->address, b->size)) {
      ReportBroken(*b);
    }
  }
}

void GuardWrite(uint8_t* address, intptr_t size, const char* name) {
  memset(address + size, kGuardPattern, kGuardBytes);

  LiveBlock* entry = static_cast<LiveBlock*>(malloc(sizeof(LiveBlock)));
  if (entry == nullptr) {
    return;  // Ohne Buchführung nur kein Sweep; der Wächter steht trotzdem.
  }
  entry->address = address;
  entry->size = size;
  entry->name = name;

  pthread_mutex_lock(&g_guard_lock);
  entry->next = g_live;
  g_live = entry;
  if (++g_alloc_count % kSweepEvery == 0) {
    SweepLocked();
  }
  pthread_mutex_unlock(&g_guard_lock);
}

void GuardCheckAndForget(uint8_t* address) {
  pthread_mutex_lock(&g_guard_lock);
  LiveBlock** link = &g_live;
  while (*link != nullptr) {
    if ((*link)->address == address) {
      LiveBlock* found = *link;
      if (!GuardIntact(found->address, found->size)) {
        ReportBroken(*found);
      }
      *link = found->next;
      pthread_mutex_unlock(&g_guard_lock);
      free(found);
      return;
    }
    link = &(*link)->next;
  }
  pthread_mutex_unlock(&g_guard_lock);
}

}  // namespace

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
  // Diese beiden Zusicherungen sind im Release-Build wirkungslos - und genau
  // dort laufen wir. Fordert die VM ausfuehrbaren Speicher an, liefert
  // memalign stillschweigend nicht ausfuehrbaren; der spaetere Sprung dorthin
  // ergibt einen Data Abort weit entfernt von der Ursache. Deshalb hier eine
  // Meldung, die auch im Release ankommt.
  ASSERT(!is_executable);
  ASSERT(!is_compressed);

  // Ausführbarer Speicher lässt sich hier nicht liefern. Im AOT-Product-Modus
  // wird er auch nicht gebraucht (siehe Kopf der Datei) – fragt ihn doch
  // jemand an, ist eine Meldung besser als stiller Erfolg mit falscher
  // Zusicherung. Diese eine bleibt: Sie feuert nie im Normalbetrieb.
  if (is_executable) {
    VmLog("VirtualMemory::AllocateAligned: ausfuehrbarer Speicher angefordert "
          "(%s) - Horizon kann das nicht liefern",
          name != nullptr ? name : "?");
  }

  // Wächter hinter dem Nutzbereich mitanfordern, siehe GuardCheck oben.
  void* address = memalign(alignment, size + kGuardBytes);
  if (address == nullptr) {
    VmLog("VirtualMemory::AllocateAligned: memalign(%ld, %ld) fehlgeschlagen",
          static_cast<long>(alignment),
          static_cast<long>(size + kGuardBytes));
    return nullptr;
  }
  GuardWrite(static_cast<uint8_t*>(address), size, name);

  MemoryRegion region(address, size);
  return new VirtualMemory(region, region);
}

VirtualMemory::~VirtualMemory() {
  if (vm_owns_region()) {
    GuardCheckAndForget(reinterpret_cast<uint8_t*>(reserved_.pointer()));
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

#endif  // defined(DART_HOST_OS_HORIZON)
