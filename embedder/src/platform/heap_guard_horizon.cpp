// Wächter um jede Heap-Allokation – Werkzeug zur Suche nach der
// Speicherbeschädigung.
//
// Ausgangslage: Ein Data Abort in `_malloc_r` bedeutet beschädigte
// Verwaltungsstrukturen; jemand schreibt über einen Block hinaus. Wächter um
// die Blöcke der Dart-VM (`virtual_memory_horizon.cc`) haben nie
// angeschlagen, es trifft also gewöhnliche Allokationen. Code lesen hat nichts
// ergeben – also wird gemessen statt vermutet.
//
// Der Linker leitet malloc/free/realloc/calloc hierher um (`-Wl,--wrap=…`,
// siehe Makefile). Jeder Block bekommt:
//
//   [ Kopf mit Größe und Herkunft ][ Nutzbereich ][ Wächter ]
//
// Der Kopf steht *vor* dem zurückgegebenen Zeiger und speichert die
// Rücksprungadresse des Aufrufers. Schlägt ein Wächter an, ist damit nicht nur
// bekannt, welcher Block überschrieben wurde, sondern auch, wer ihn angefordert
// hat – die Adresse lässt sich mit `build-logs/resolve-crash.sh` in eine
// Quellzeile auflösen.
//
// Das kostet Speicher und Zeit. Es ist ein Diagnosewerkzeug, kein Dauerzustand:
// Ohne die --wrap-Schalter im Makefile ist die Datei wirkungslos.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "flutter_libnx/log.h"

extern "C" {
void* __real_malloc(size_t size);
void __real_free(void* ptr);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* ptr, size_t size);
void* __real_memalign(size_t alignment, size_t size);
}

namespace {

constexpr uint32_t kMagicLive = 0xC0FFEE01;
constexpr uint32_t kMagicFreed = 0xDEADBE01;
constexpr size_t kGuardBytes = 16;
constexpr uint8_t kGuardPattern = 0x5A;

// Ausrichtung: Der Kopf muss ein Vielfaches der strengsten Ausrichtung sein,
// die malloc zusichert, sonst liefern wir schlechter ausgerichteten Speicher
// als der Aufrufer erwartet. 16 Bytes genügen auf AArch64.
struct alignas(16) Header {
  size_t size;
  void* caller;
  // Der Anfang des echten Blocks. Bei malloc ist das der Kopf selbst, bei
  // memalign liegt er weiter davor – free() muss aber immer den echten Anfang
  // bekommen.
  void* raw;
  uint32_t magic;
  uint32_t padding;
};

static_assert(sizeof(Header) % 16 == 0, "Kopf muss die Ausrichtung wahren");

bool GuardIntact(const uint8_t* user, size_t size) {
  for (size_t i = 0; i < kGuardBytes; i++) {
    if (user[size + i] != kGuardPattern) {
      return false;
    }
  }
  return true;
}

void Report(const Header* head, const uint8_t* user, const char* wann) {
  LOG_ERROR("HEAP-UEBERLAUF (%s): Block %p, %lu Bytes, angefordert von %p",
            wann, static_cast<const void*>(user),
            static_cast<unsigned long>(head->size), head->caller);
  const uint8_t* g = user + head->size;
  LOG_ERROR("  Waechter: %02x %02x %02x %02x %02x %02x %02x %02x", g[0], g[1],
            g[2], g[3], g[4], g[5], g[6], g[7]);
}

}  // namespace

extern "C" {

void* __wrap_malloc(size_t size) {
  void* raw = __real_malloc(sizeof(Header) + size + kGuardBytes);
  if (raw == nullptr) {
    // Erschöpfter Heap sieht von außen wie eine Speicherbeschädigung aus: Wer
    // den Rückgabewert nicht prüft, schreibt auf null oder rechnet damit
    // weiter. Diese Meldung unterscheidet die beiden Fälle - sie kostet
    // nichts, solange sie nicht feuert.
    LOG_ERROR("MALLOC FEHLGESCHLAGEN: %lu Bytes angefordert von %p",
              static_cast<unsigned long>(size), __builtin_return_address(0));
    return nullptr;
  }
  Header* head = static_cast<Header*>(raw);
  head->size = size;
  head->caller = __builtin_return_address(0);
  head->magic = kMagicLive;
  head->raw = raw;

  uint8_t* user = static_cast<uint8_t*>(raw) + sizeof(Header);
  memset(user + size, kGuardPattern, kGuardBytes);
  return user;
}

void __wrap_free(void* ptr) {
  if (ptr == nullptr) {
    return;
  }
  uint8_t* user = static_cast<uint8_t*>(ptr);
  Header* head = reinterpret_cast<Header*>(user - sizeof(Header));

  if (head->magic == kMagicFreed) {
    LOG_ERROR("DOPPELTES FREE: Block %p, zuletzt angefordert von %p", ptr,
              head->caller);
    return;
  }
  if (head->magic != kMagicLive) {
    // Kein Block aus unserer Hand – etwa aus einer Bibliothek, die vor dem
    // Umleiten alloziert hat. Unverändert weiterreichen wäre falsch, denn der
    // Zeiger zeigt dann nicht auf unseren Kopf; also nur melden.
    LOG_ERROR("FREE eines fremden Blocks %p (magic=0x%08x)", ptr, head->magic);
    return;
  }

  if (!GuardIntact(user, head->size)) {
    Report(head, user, "beim Freigeben");
  }
  head->magic = kMagicFreed;
  __real_free(head->raw);  // bei memalign nicht der Kopf, sondern davor
}

// memalign MUSS mitgewrappt werden, sobald free() es wird.
//
// Die Dart-VM fordert ihren gesamten Heap über memalign an und gibt ihn mit
// free() frei. Liefe nur free() über diesen Wrapper, würde er 32 Bytes vor
// einem Block lesen, den memalign ohne unseren Kopf angelegt hat – also
// fremde Daten als Größe und Magic deuten und daraufhin an einer errechneten
// Adresse einen "Wächter" prüfen. Ein Werkzeug, das Speicherfehler sucht,
// darf keine erzeugen.
//
// Der Kopf muss hier *vor* dem ausgerichteten Zeiger liegen, ohne dessen
// Ausrichtung zu zerstören: Deshalb wird um `alignment` mehr angefordert und
// der Nutzzeiger auf die nächste passende Grenze gelegt, die noch Platz für
// den Kopf davor lässt.
void* __wrap_memalign(size_t alignment, size_t size) {
  if (alignment < alignof(Header)) {
    alignment = alignof(Header);
  }
  const size_t total = alignment + size + kGuardBytes;
  void* raw = __real_memalign(alignment, total);
  if (raw == nullptr) {
    return nullptr;
  }

  // Eine volle Ausrichtungseinheit weiter: Damit bleibt der Nutzzeiger
  // ausgerichtet und davor ist mindestens sizeof(Header) frei.
  uint8_t* user = static_cast<uint8_t*>(raw) + alignment;
  Header* head = reinterpret_cast<Header*>(user - sizeof(Header));
  head->size = size;
  head->caller = __builtin_return_address(0);
  head->magic = kMagicLive;
  head->raw = raw;

  memset(user + size, kGuardPattern, kGuardBytes);
  return user;
}

void* __wrap_calloc(size_t count, size_t size) {
  const size_t total = count * size;
  // Überlauf der Multiplikation abfangen – calloc muss das prüfen.
  if (count != 0 && total / count != size) {
    return nullptr;
  }
  void* user = __wrap_malloc(total);
  if (user != nullptr) {
    memset(user, 0, total);
  }
  return user;
}

void* __wrap_realloc(void* ptr, size_t size) {
  if (ptr == nullptr) {
    return __wrap_malloc(size);
  }
  if (size == 0) {
    __wrap_free(ptr);
    return nullptr;
  }

  uint8_t* user = static_cast<uint8_t*>(ptr);
  Header* head = reinterpret_cast<Header*>(user - sizeof(Header));
  if (head->magic != kMagicLive) {
    LOG_ERROR("REALLOC eines fremden Blocks %p (magic=0x%08x)", ptr,
              head->magic);
    return nullptr;
  }
  if (!GuardIntact(user, head->size)) {
    Report(head, user, "beim Vergroessern");
  }

  // Von Hand statt über __real_realloc: Der echte Block beginnt beim Kopf, und
  // die Nutzdaten müssen an ihrer Stelle im neuen Block landen.
  void* fresh = __wrap_malloc(size);
  if (fresh == nullptr) {
    return nullptr;
  }
  memcpy(fresh, ptr, head->size < size ? head->size : size);
  __wrap_free(ptr);
  return fresh;
}

}  // extern "C"
