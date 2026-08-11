// Was wirklich scheitert, wenn pthread_create fehlschlägt.
//
// Anlass: Die Anwendung bricht unregelmäßig mit
//
//   runtime/bin/thread.cc:19: Could not start thread dart:io EventHandler:
//   12 (Not enough space)
//
// ab. `12` ist ENOMEM – aber das bedeutet hier nichts: libnx' pthread-Schicht
// verwirft den Result-Code und meldet für **jeden** Fehlschlag ENOMEM
// (`nx/source/runtime/newlib.c:209-213`), gleich ob `threadCreate`,
// `svcSetThreadCoreMask` oder `threadStart` gescheitert ist. Ein Messprogramm
// (`examples/thread_probe`) hat gezeigt, dass 64 Threads mit je 1 MB Stack
// mühelos gehen – Speichermangel scheidet also aus.
//
// Diese Datei holt die verworfene Auskunft zurück: Schlägt pthread_create
// fehl, versucht sie dasselbe noch einmal direkt über libnx – mit denselben
// Parametern, die die pthread-Schicht benutzt – und protokolliert den echten
// Result-Code samt Modul und Beschreibung.
//
// Sie liegt im Embedder, weil `<switch.h>` im Dart-Baum nichts zu suchen hat;
// dieselbe Trennung wie bei Stackgrenzen, Schriften und Zufall.

#include <switch.h>

#include <stddef.h>
#include <stdlib.h>

#include "flutter_libnx/log.h"

namespace {

void ThreadNoop(void*) {}

}  // namespace

extern "C" {

void flutter_libnx_scan_heap(const char* label);

void flutter_libnx_diagnose_thread_failure(const char* name,
                                           size_t stack_size) {
  LOG_ERROR("=== pthread_create fehlgeschlagen fuer '%s' (Stack %lu KB) ===",
            name != nullptr ? name : "?",
            static_cast<unsigned long>(stack_size / 1024));

  u64 total = 0;
  u64 used = 0;
  svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  LOG_ERROR("  Speicher: %llu MB gesamt, %llu MB belegt",
            static_cast<unsigned long long>(total / (1024 * 1024)),
            static_cast<unsigned long long>(used / (1024 * 1024)));

  // Derselbe Aufruf, den die pthread-Schicht macht (newlib.c:190):
  // Priorität 0x3B, beliebiger Kern, Stack von libnx besorgt.
  Thread probe = {};
  const Result rc =
      threadCreate(&probe, ThreadNoop, nullptr, nullptr, stack_size, 0x3B, -2);
  if (R_FAILED(rc)) {
    LOG_ERROR("  threadCreate: 0x%08x (Modul %u, Beschreibung %u)", rc,
              R_MODULE(rc), R_DESCRIPTION(rc));
    return;
  }

  LOG_ERROR("  threadCreate: erfolgreich - der Fehler liegt also spaeter");

  const Result rc_start = threadStart(&probe);
  if (R_FAILED(rc_start)) {
    LOG_ERROR("  threadStart: 0x%08x (Modul %u, Beschreibung %u)", rc_start,
              R_MODULE(rc_start), R_DESCRIPTION(rc_start));
  } else {
    LOG_ERROR("  threadStart: erfolgreich");
    threadWaitForExit(&probe);
  }
  threadClose(&probe);

  // Bleibt als dritte Möglichkeit die kleine Verwaltungsstruktur, die die
  // pthread-Schicht vor allem anderen anfordert.
  void* small = malloc(256);
  LOG_ERROR("  malloc(256) waehrend des Fehlschlags: %s",
            small != nullptr ? "geht" : "FEHLGESCHLAGEN");
  free(small);

  flutter_libnx_scan_heap("Threadfehler");
}

// Heap-Scanner (Fehlersuche zu InvalidMemoryState und _malloc_r).
//
// Anlass: threadCreate scheiterte mit 0xd401 = Kernel, Beschreibung 106 =
// KernelError_InvalidMemoryState. Das heisst: Der frisch von memalign
// gelieferte Stack-Block enthielt mindestens eine Seite, deren Memory-State
// nicht mehr (MemType_Heap, attr=0, perm=RW) war. Irgendetwas veraendert den
// Zustand von Heap-Seiten und laesst sie trotzdem in den Freispeicher
// zurueckfallen.
//
// Dieser Scan laeuft den gesamten Heap-Bereich mit svcQueryMemory ab (der
// Kernel fasst gleichartige Seiten zu Regionen zusammen, der Lauf ist also
// billig) und meldet jede Region, die vom Normalzustand abweicht.
//
// Zur Lesart: Stacks *lebender* libnx-Threads erscheinen hier erwartungsgemaess
// als attr=IsBorrowed(1)/perm=0 - svcMapMemory setzt den Quellbereich im Heap
// genau so. Verdaechtig sind Regionen, die ueber die Zahl der lebenden Threads
// hinausgehen, sowie jedes andere Attribut (IsUncached=8 waere nvMap, also der
// Framebuffer-Verdacht).
void flutter_libnx_scan_heap(const char* label) {
  extern char* fake_heap_start;
  extern char* fake_heap_end;

  const u64 heap_start = reinterpret_cast<u64>(fake_heap_start);
  const u64 heap_end = reinterpret_cast<u64>(fake_heap_end);

  int regions = 0;
  int anomalies = 0;
  u64 anomaly_bytes = 0;
  u64 addr = heap_start;
  while (addr < heap_end) {
    MemoryInfo info = {};
    u32 page_info = 0;
    const Result rc = svcQueryMemory(&info, &page_info, addr);
    if (R_FAILED(rc)) {
      LOG_ERROR("Heap-Scan[%s]: svcQueryMemory(%p) = 0x%08x", label,
                reinterpret_cast<void*>(addr), rc);
      return;
    }
    regions++;
    if (info.type != MemType_Heap || info.attr != 0 || info.perm != Perm_Rw) {
      anomalies++;
      anomaly_bytes += info.size;
      // Begrenzung schuetzt die TCP-Senke vor einer Flut, falls der Heap in
      // viele abweichende Regionen zerfallen ist.
      if (anomalies <= 24) {
        LOG_ERROR("Heap-Scan[%s]: %p +%llu KB type=%u attr=0x%x perm=0x%x",
                  label, reinterpret_cast<void*>(info.addr),
                  static_cast<unsigned long long>(info.size / 1024), info.type,
                  info.attr, info.perm);
      }
    }
    if (info.addr + info.size <= addr) {
      LOG_ERROR("Heap-Scan[%s]: Region rueckwaerts bei %p - Abbruch", label,
                reinterpret_cast<void*>(addr));
      return;
    }
    addr = info.addr + info.size;
  }
  LOG_INFO("Heap-Scan[%s]: %d Regionen, %d abweichend (%llu KB)", label,
           regions, anomalies,
           static_cast<unsigned long long>(anomaly_bytes / 1024));
}

// Erbschaftsbereinigung (Reparatur zum sporadischen Absturz).
//
// Die NRO laeuft im Prozess des Spiels; hbloader uebergibt den Heap
// "gebraucht", und Kernel-Seitenzustaende ueberleben den NRO-Wechsel. Ein
// erfolgreicher Vorlauf hinterlaesst mindestens einen noch per svcMapMemory
// gemappten Thread-Stack (libnx raeumt die Stacks abgeloester Threads erst
// beim naechsten pthread-Aufruf ab - fuer die letzten gibt es keinen). Der
// naechste Start haelt diese unlesbaren Seiten fuer freien Speicher:
// Freilisten-Kette hinein -> Data Abort in _malloc_r (FAR = X3+8);
// memalign reicht sie an threadCreate weiter -> InvalidMemoryState (0xd401).
//
// Hier werden solche Erbstuecke beim Start zurueckgebaut: Zu jedem
// geliehenen Loch im Heap wird im Adressraum ein Spiegel (MemType_
// MappedMemory) exakt gleicher Groesse gesucht und svcUnmapMemory versucht.
// Drei Sicherungen:
//
//   * Der eigene Main-Stack ist ebenfalls eine geliehene Heap-Region. Sein
//     Spiegel (der Bereich, auf dem SP steht) wird nie als Kandidat benutzt.
//   * Der Kernel (Mesosphaere) prueft beim Unmap die physische Entsprechung
//     des Paars - eine falsche Paarung wird abgelehnt, nicht ausgefuehrt.
//   * Muss so frueh in main() laufen, dass noch keine eigenen Threads
//     existieren; dann sind alle passenden Spiegel tote Hinterlassenschaften.
void flutter_libnx_heal_heap(void) {
  extern char* fake_heap_start;
  extern char* fake_heap_end;
  const u64 heap_start = reinterpret_cast<u64>(fake_heap_start);
  const u64 heap_end = reinterpret_cast<u64>(fake_heap_end);

  // Geliehene Loecher im Heap einsammeln.
  struct Region {
    u64 addr;
    u64 size;
  };
  Region holes[16];
  int hole_count = 0;
  for (u64 addr = heap_start; addr < heap_end;) {
    MemoryInfo info = {};
    u32 page_info = 0;
    if (R_FAILED(svcQueryMemory(&info, &page_info, addr))) {
      return;
    }
    if (info.type == MemType_Heap && (info.attr & MemAttr_IsBorrowed) != 0 &&
        hole_count < 16) {
      holes[hole_count].addr = info.addr;
      holes[hole_count].size = info.size;
      hole_count++;
    }
    if (info.addr + info.size <= addr) {
      return;
    }
    addr = info.addr + info.size;
  }
  if (hole_count == 0) {
    LOG_INFO("Erbschaftsbereinigung: Heap ist sauber");
    return;
  }

  // Den eigenen Stack-Spiegel bestimmen - der Bereich, auf dem dieser Frame
  // liegt. Er darf unter keinen Umstaenden angefasst werden.
  MemoryInfo own_stack = {};
  {
    u32 page_info = 0;
    u64 probe_on_stack = 0;
    if (R_FAILED(svcQueryMemory(&own_stack, &page_info,
                                reinterpret_cast<u64>(&probe_on_stack)))) {
      return;
    }
  }

  // Adressraum nach Spiegeln (MemType_MappedMemory) absuchen und je Loch
  // die groessengleichen Kandidaten probieren. Der 39-Bit-Adressraum ist in
  // wenige Regionen zusammengefasst; der Lauf ist billig.
  int healed = 0;
  for (int h = 0; h < hole_count; h++) {
    bool done = false;
    for (u64 addr = 0; addr < (1ULL << 39) && !done;) {
      MemoryInfo info = {};
      u32 page_info = 0;
      if (R_FAILED(svcQueryMemory(&info, &page_info, addr))) {
        break;
      }
      if (info.type == MemType_MappedMemory && info.size == holes[h].size &&
          info.addr != own_stack.addr) {
        const Result rc = svcUnmapMemory(reinterpret_cast<void*>(info.addr),
                                         reinterpret_cast<void*>(holes[h].addr),
                                         holes[h].size);
        if (R_SUCCEEDED(rc)) {
          LOG_INFO("Erbschaftsbereinigung: %p +%llu KB zurueckgebaut "
                   "(Spiegel %p)",
                   reinterpret_cast<void*>(holes[h].addr),
                   static_cast<unsigned long long>(holes[h].size / 1024),
                   reinterpret_cast<void*>(info.addr));
          healed++;
          done = true;
        }
        // Ablehnung heisst nur: falsches Paar - weiterprobieren.
      }
      if (info.addr + info.size <= addr) {
        break;
      }
      addr = info.addr + info.size;
    }
    if (!done) {
      LOG_INFO("Erbschaftsbereinigung: %p +%llu KB bleibt (kein Spiegel "
               "gefunden - vermutlich der eigene Main-Stack)",
               reinterpret_cast<void*>(holes[h].addr),
               static_cast<unsigned long long>(holes[h].size / 1024));
    }
  }
  LOG_INFO("Erbschaftsbereinigung: %d von %d Erbstuecken zurueckgebaut",
           healed, hole_count);
}

}  // extern "C"
