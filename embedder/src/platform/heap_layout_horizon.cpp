// Aufteilung des Prozessspeichers zwischen newlib-Heap und allem übrigen.
//
// Das Problem, das diese Datei löst, ist der Grund für eine ganze Reihe von
// Abstürzen gewesen – sporadisch, an wechselnden Stellen, zuletzt mit
// eindeutiger Meldung:
//
//   runtime/bin/thread.cc:19: error: Could not start thread dart:io
//   EventHandler: 12 (Not enough space)
//
// 12 ist ENOMEM. Ursache ist die Vorgabe von libnx
// (`nx/source/runtime/init.c:78`):
//
//   if (mem_available > mem_used+0x200000)
//       size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
//
// Der newlib-Heap bekommt also **alles bis auf 2 MB**. Für ein gewöhnliches
// Homebrew-Programm ist das sinnvoll: Es alloziert über malloc und sonst
// nichts. Die Flutter-Engine tut das Gegenteil – sie legt mehrere Threads an,
// und libnx spiegelt jeden Thread-Stack zusätzlich über `svcMapMemory` an eine
// zweite Adresse (`nx/source/kernel/thread.c:132`). Diese Abbildungen und die
// Buchführung des Kernels brauchen Speicher, der **nicht** im Heap liegen darf.
// Bei 2 MB Rest reicht das für keinen einzigen 2-MB-Stack.
//
// Deshalb bleibt hier deutlich mehr frei. Der Preis ist ein kleinerer
// Dart-Heap; der Gewinn ist, dass Threads überhaupt entstehen können.
//
// Erkennbar war das an der Zahl nicht: `InfoType_UsedMemorySize` meldete
// „3185 von 3189 MB belegt“ – das ist die Heap-Reservierung, nicht der
// Verbrauch. Wer sie als Auslastung liest, sucht an der falschen Stelle.

#include <switch.h>

#include "flutter_libnx/log.h"

namespace {

// Was außerhalb des Heaps frei bleiben muss.
//
// Bedarf pro Engine-Thread: 2 MB Stack (fml::Thread::GetDefaultStackSize) plus
// dessen Spiegelabbildung. Die Engine legt Plattform-, UI-, Raster- und
// IO-Thread an, dazu Dart-Worker und der dart:io-Eventhandler – ein gutes
// Dutzend. 256 MB lassen dafür Luft und kosten bei über 3 GB kaum etwas.
//
// Im Applet-Modus (rund 380 MB gesamt) wäre dieser Wert zu groß; dort greift
// die Untergrenze weiter unten. Getestet wird ohnehin im Anwendungsmodus,
// weil die Engine im Applet-Modus schon an anderen Stellen scheitert.
constexpr u64 kReserveOutsideHeap = 256ull * 1024 * 1024;

// Unterhalb dieser Heapgröße lohnt der Versuch nicht mehr – dann ist die
// Betriebsart falsch gewählt, und ein winziger Heap verschleiert das nur.
constexpr u64 kMinimumHeap = 64ull * 1024 * 1024;

// Obergrenze für den Heap, wenn der Loader mehr vorgibt.
//
// 1 GB ist für eine Flutter-Anwendung reichlich: Der Dart-Heap wächst in
// 512-KB-Schritten, Skias Puffer für 1280x720 liegen im einstelligen
// Megabytebereich. Alles darüber hinaus nützt nichts und fehlt dem Kernel für
// die Abbildungen der Thread-Stacks.
constexpr u64 kMaxHeapForEngine = 1024ull * 1024 * 1024;

}  // namespace

// Wird vor allen Konstruktoren gesetzt; der Logger steht zu diesem Zeitpunkt
// noch nicht, deshalb nur merken und später ausgeben.
bool g_heap_from_loader = false;
bool g_heap_shrunk = false;

extern "C" {

// Überschreibt die schwach gebundene Fassung aus libnx. Der Aufbau folgt ihr
// bewusst genau; abweichend ist allein die Reserve.
void __libnx_initheap(void) {
  void* addr = nullptr;
  u64 size = 0;

  if (envHasHeapOverride()) {
    // Der Loader gibt den Heap vor. Das ist bei Homebrew über hbloader der
    // Normalfall – und es bedeutet, dass die Reserve unten gar nicht zum
    // Tragen kommt: Wie viel außerhalb des Heaps frei bleibt, hat dann der
    // Loader entschieden, nicht wir.
    //
    // Diese Meldung steht hier, weil genau das übersehen wurde: Eine
    // Heap-Aufteilung zu ändern, die man nie vornimmt, sieht nach einer
    // Reparatur aus, ohne eine zu sein.
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
    g_heap_from_loader = true;

    // Der Loader gibt fast den gesamten Speicher als Heap. Für eine
    // Anwendung, die nur malloc benutzt, ist das ideal – für diese nicht:
    // libnx spiegelt jeden Thread-Stack per `svcMapMemory` an eine zweite
    // Adresse (`nx/source/kernel/thread.c:132`), und der Kernel braucht dafür
    // Speicher *außerhalb* des Heaps. Davon bleiben rund 20 MB, unabhängig
    // davon, wie klein die Stacks sind – daher
    //
    //   thread.cc:19: Could not start thread dart:io EventHandler: 12
    //
    // auch nach dem Halbieren der Stackgröße.
    //
    // NICHT VERSUCHEN, DEN HEAP HIER ZURÜCKZUGEBEN.
    //
    // Genau das stand hier einmal: `svcSetHeapSize` auf 1 GB, um dem Kernel
    // Platz für die Abbildungen zu verschaffen. Ergebnis war, dass die
    // Anwendung nicht mehr startete – sie starb, bevor auch nur die Logsenke
    // stand.
    //
    // Der Grund: Der Homebrew-Loader legt die NRO selbst in diesen Heap, und
    // `svcSetHeapSize` liefert eine *neue* Adresse zurück. Wer ihn
    // verkleinert, zieht dem laufenden Programm den Boden weg, noch bevor
    // main() beginnt.
    //
    // Die Überlegung, der Programmcode liege in einem eigenen Bereich, war
    // eine Vermutung ohne Beleg – und sie war falsch. Der Engpass bei den
    // Thread-Abbildungen bleibt damit bestehen; er ist über die Anzahl der
    // Threads zu lösen, nicht über die Heapgröße.
  } else {
    u64 mem_available = 0;
    u64 mem_used = 0;
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    u64 reserve = kReserveOutsideHeap;
    // Bei wenig Gesamtspeicher (Applet-Modus) würde die volle Reserve nichts
    // übrig lassen. Dann ein Achtel, aber nie weniger als libnx' 2 MB.
    if (mem_available < reserve * 4) {
      reserve = mem_available / 8;
      if (reserve < 0x200000) {
        reserve = 0x200000;
      }
    }

    if (mem_available > mem_used + reserve) {
      size = (mem_available - mem_used - reserve) & ~0x1FFFFFull;
    }
    if (size < kMinimumHeap) {
      // Lieber mit libnx' Vorgabe starten als mit einem Heap, in dem die
      // Engine ohnehin nicht arbeiten kann.
      size = (mem_available > mem_used + 0x200000)
                 ? ((mem_available - mem_used - 0x200000) & ~0x1FFFFFull)
                 : 0;
    }

    const Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc)) {
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
    }
  }

  extern char* fake_heap_start;
  extern char* fake_heap_end;
  fake_heap_start = static_cast<char*>(addr);
  fake_heap_end = static_cast<char*>(addr) + size;
}

}  // extern "C"
