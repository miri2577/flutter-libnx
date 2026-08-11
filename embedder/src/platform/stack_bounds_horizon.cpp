// Stackgrenzen des aktuellen Threads für Horizon.
//
// Die Dart-VM ermittelt beim Anlegen jedes OSThread die Grenzen seines Stacks
// (`OSThread::GetCurrentStackBounds`). Auf Linux geht das über
// `pthread_getattr_np`, das newlib nicht hat. Ein `false` ist dort **kein**
// weicher Rückfall: `vm/os_thread.cc` macht daraus unmittelbar
// `FATAL("Failed to retrieve stack bounds")`.
//
// Der Weg ohne libnx-Interna führt über `svcQueryMemory`: Der Kernel liefert
// zu einer Adresse die umgebende Speicherregion. Für beide Thread-Arten ist
// das genau die gesuchte Region:
//
//   * Nebenläufige Threads laufen auf einem `stack_mirror`, den libnx per
//     `svcMapMemory` einblendet (third_party/libnx/nx/source/kernel/thread.c
//     Z. 132). Die Abbildung umfasst exakt Stack, TLS und reent-Struktur.
//   * Der Hauptthread bekommt seinen Stack vom Loader; auch dort begrenzt der
//     Kernel die Region auf ebendiesen Stack.
//
// Warum eine eigene Datei: `<switch.h>` bringt Makros und Typnamen mit (`u32`,
// `Result`), die sich mit Dart-Bezeichnern beißen. Dieselbe Vorsicht wie bei
// `random_horizon.cpp` und `os_horizon.cc`.

#include <switch.h>

#include <stdint.h>

extern "C" {

// Eine Stackregion, die größer als das hier ist, ist keine – dann hat die
// Abfrage etwas anderes getroffen (etwa den gesamten Heap). Lieber ehrlich
// scheitern als der VM eine Untergrenze nennen, die Stapelüberläufe
// unbemerkt durchgehen lässt.
static const uint64_t kMaxPlausibleStackSize = 64u * 1024u * 1024u;

bool flutter_libnx_get_stack_bounds(uintptr_t* lower, uintptr_t* upper) {
  if (lower == nullptr || upper == nullptr) {
    return false;
  }

  const uintptr_t sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

  MemoryInfo info = {};
  u32 page_info = 0;
  if (R_FAILED(svcQueryMemory(&info, &page_info, sp))) {
    return false;
  }

  if (info.type == MemType_Unmapped || info.size == 0) {
    return false;
  }
  if (info.size > kMaxPlausibleStackSize) {
    return false;
  }
  if (sp < info.addr || sp >= info.addr + info.size) {
    return false;
  }

  *lower = static_cast<uintptr_t>(info.addr);
  *upper = static_cast<uintptr_t>(info.addr + info.size);
  return true;
}

}  // extern "C"
