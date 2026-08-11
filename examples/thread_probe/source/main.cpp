// Wo genau die Threads ausgehen – ein Messprogramm ohne Engine.
//
// Ausgangslage: Die Flutter-Anwendung scheitert unregelmäßig mit
//
//   runtime/bin/thread.cc:19: Could not start thread dart:io EventHandler:
//   12 (Not enough space)
//
// Erschlossen war bisher: Der Homebrew-Loader gibt über 99 % des Speichers als
// newlib-Heap, und was der Kernel für die Spiegelabbildungen der Thread-Stacks
// braucht, liegt außerhalb davon. Gemessen war das nie – alle Reparaturen
// beruhten auf dieser Annahme, und keine hat geholfen.
//
// Dieses Programm klärt drei Fragen, die sich in der großen Anwendung nicht
// trennen lassen:
//
//   1. Wie viele Threads gehen überhaupt, und ab welcher Stackgröße?
//   2. Scheitert die Stack-Allokation aus dem Heap (`aligned_alloc`) oder das
//      Einblenden der Spiegelabbildung (`svcMapMemory`)? libnx' `threadCreate`
//      liefert dafür unterschiedliche Fehlercodes, pthread_create nicht.
//   3. Ist der Heap zu dem Zeitpunkt noch benutzbar?
//
// Bewusst ohne Engine: Der Bau dauert Sekunden statt zwanzig Minuten, und ein
// Fehlschlag kann kaum das System mitreißen.

#include <switch.h>

#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "flutter_libnx/log.h"

// Newlibs Heapgrenzen. Die Deklaration gehört auf globale Ebene: In einem
// anonymen Namespace erzeugt `extern` ein namespace-lokales Symbol, das der
// Linker nirgends findet.
extern char* fake_heap_start;
extern char* fake_heap_end;

namespace {

constexpr const char* kHostIp = "192.168.0.153";
constexpr uint16_t kHostPort = 28800;

// So viele Threads höchstens – weit über dem, was die Engine braucht.
constexpr int kMaxThreads = 64;

// Die Threads beenden sich selbst, sobald das Signal steht. Das ist wichtig:
// Bleiben sie am Leben, misst die nächste Probe nur noch den Rest – genau
// dieser Fehler hat den ersten Durchlauf unbrauchbar gemacht.
volatile bool g_stop = false;

void ThreadBody(void*) {
  while (!g_stop) {
    svcSleepThread(1000000);  // 1 ms
  }
}

void* PthreadBody(void*) {
  while (!g_stop) {
    svcSleepThread(1000000);
  }
  return nullptr;
}

void ReportMemory(const char* wann) {
  u64 total = 0;
  u64 used = 0;
  svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  const u64 heap = static_cast<u64>(fake_heap_end - fake_heap_start);
  LOG_INFO("[%s] gesamt %llu MB, belegt %llu MB, Heap %llu MB, ausserhalb "
           "%llu MB",
           wann, static_cast<unsigned long long>(total / (1024 * 1024)),
           static_cast<unsigned long long>(used / (1024 * 1024)),
           static_cast<unsigned long long>(heap / (1024 * 1024)),
           static_cast<unsigned long long>((total - heap) / (1024 * 1024)));
}

// Probe 1: libnx direkt. Der Fehlercode sagt, welcher Schritt scheitert.
void ProbeLibnxThreads(size_t stack_size) {
  LOG_INFO("--- libnx threadCreate, Stack %lu KB ---",
           static_cast<unsigned long>(stack_size / 1024));

  static Thread threads[kMaxThreads];
  int created = 0;
  for (int i = 0; i < kMaxThreads; i++) {
    // Dieselben Werte wie in newlib.c:190, damit der Vergleich mit pthread
    // trägt: Priorität 0x3B, beliebiger Kern.
    const Result rc = threadCreate(&threads[i], ThreadBody, nullptr, nullptr,
                                   stack_size, 0x3B, -2);
    if (R_FAILED(rc)) {
      // Modul und Beschreibung trennen die Fälle: Ein Fehlschlag von
      // `__libnx_aligned_alloc` meldet sich als LibnxError_OutOfMemory
      // (Modul 345), einer von svcMapMemory als Kernel-Ergebnis (Modul 1).
      LOG_ERROR("  Thread %d fehlgeschlagen: 0x%08x (Modul %u, Beschreibung %u)",
                i, rc, R_MODULE(rc), R_DESCRIPTION(rc));
      break;
    }
    if (R_FAILED(threadStart(&threads[i]))) {
      LOG_ERROR("  Thread %d erzeugt, aber threadStart fehlgeschlagen", i);
      break;
    }
    created++;
  }
  LOG_INFO("  %d Threads erzeugt und gestartet", created);
  ReportMemory("nach libnx-Threads");

  // Sauber abräumen, sonst misst die nächste Probe nur den Rest.
  g_stop = true;
  for (int i = 0; i < created; i++) {
    threadWaitForExit(&threads[i]);
    threadClose(&threads[i]);
  }
  g_stop = false;
  LOG_INFO("  alle beendet");
}

// Probe 2: derselbe Weg, den Dart und die Engine nehmen.
void ProbePthreads(size_t stack_size) {
  LOG_INFO("--- pthread_create, Stack %lu KB ---",
           static_cast<unsigned long>(stack_size / 1024));

  static pthread_t threads[kMaxThreads];
  int created = 0;
  for (int i = 0; i < kMaxThreads; i++) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);
    const int rc = pthread_create(&threads[i], &attr, PthreadBody, nullptr);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
      LOG_ERROR("  Thread %d fehlgeschlagen: %d (%s)", i, rc, strerror(rc));
      break;
    }
    created++;
  }
  LOG_INFO("  %d Threads erzeugt", created);
  ReportMemory("nach pthreads");

  g_stop = true;
  for (int i = 0; i < created; i++) {
    pthread_join(threads[i], nullptr);
  }
  g_stop = false;
  LOG_INFO("  alle beendet");
}

// Probe 3: Ist der Heap danach noch brauchbar?
void ProbeHeap() {
  LOG_INFO("--- Heap nach den Threads ---");
  size_t last_ok = 0;
  for (size_t mb = 1; mb <= 512; mb *= 4) {
    void* p = std::malloc(mb * 1024 * 1024);
    if (p == nullptr) {
      LOG_ERROR("  %lu MB fehlgeschlagen", static_cast<unsigned long>(mb));
      break;
    }
    static_cast<char*>(p)[0] = 1;
    static_cast<char*>(p)[mb * 1024 * 1024 - 1] = 1;
    std::free(p);
    last_ok = mb;
  }
  LOG_INFO("  bis %lu MB am Stueck nutzbar",
           static_cast<unsigned long>(last_ok));
}

}  // namespace

int main(int argc, char* argv[]) {
  consoleInit(nullptr);

  flutter_libnx::LogConfig log_config;
  log_config.to_nxlink = false;
  log_config.remote_host = kHostIp;
  log_config.remote_port = kHostPort;
  log_config.file_path = "sdmc:/switch/flutter-libnx/thread_probe.log";
  flutter_libnx::LogInit(log_config);

  std::printf("\n  flutter-libnx / Thread-Probe\n");
  std::printf("  ============================\n\n");
  std::printf("  Ergebnisse gehen an %s:%u\n", kHostIp, kHostPort);
  consoleUpdate(nullptr);

  LOG_INFO("thread_probe startet");
  LOG_INFO("Applet-Typ: %d (0 = Anwendung)",
           static_cast<int>(appletGetAppletType()));
  ReportMemory("Start");

  // Die Größen, die in der Anwendung vorkommen: fml verwendet 1 MB (nach dem
  // Patch), Dart ebenfalls 1 MB, der Eventhandler denselben Weg.
  // Erst pthread allein – so, wie es die Anwendung erlebt, ohne dass vorher
  // etwas anderes Threads belegt.
  ProbePthreads(1024 * 1024);

  // Dann libnx direkt, mit denselben Parametern wie die pthread-Schicht
  // (Priorität 0x3B, siehe newlib.c:190). Nur hier ist der echte Fehlercode
  // zu sehen: pthread verwirft ihn und meldet pauschal ENOMEM.
  ProbeLibnxThreads(1024 * 1024);

  ProbeHeap();

  LOG_INFO("thread_probe fertig");
  std::printf("\n  Fertig. Plus zum Beenden.\n");
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

  flutter_libnx::LogShutdown();
  consoleExit(nullptr);
  return 0;
}
