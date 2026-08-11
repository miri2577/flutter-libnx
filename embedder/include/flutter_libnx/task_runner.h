// Aufgabenschlange für den Hauptthread.
//
// Ohne diese Klasse erzeugt die Engine eigene Threads für Plattform-, UI- und
// Rasteraufgaben. Auf Horizon ist das aus zwei Gründen ein Problem:
//
//   * **Speicher.** Jeder Thread bekommt einen Stack, und libnx blendet ihn
//     zusätzlich per `svcMapMemory` an einer zweiten Adresse ein. Dafür
//     braucht der Kernel Speicher *außerhalb* des newlib-Heaps – und den gibt
//     der Homebrew-Loader zu über 99 % dem Heap. Übrig bleiben rund 20 MB, was
//     regelmäßig zu `Could not start thread …: 12 (Not enough space)` führt.
//   * **Rückmeldungen.** Die Engine erwartet, dass auf dem Thread, der
//     `FlutterEngineInitialize` aufgerufen hat, eine Nachrichtenschleife läuft.
//     Wer dort stattdessen eine eigene Schleife betreibt, bekommt nie eine
//     Antwort: Rückrufe zu Tastenereignissen und Platform-Messages versanden.
//
// Beides löst sich, indem der Embedder die Aufgaben selbst annimmt und in
// seiner Hauptschleife abarbeitet. Die Embedder-API sieht das ausdrücklich vor
// und erlaubt, Plattform-, Render- und UI-Aufgaben demselben Runner zuzuweisen
// (`embedder.h:1892-1908`). Für einen Software-Renderer ohne
// GPU-Nebenläufigkeit ist das kein Verlust.
#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "embedder.h"

namespace flutter_libnx {

class TaskRunner {
 public:
  TaskRunner();

  TaskRunner(const TaskRunner&) = delete;
  TaskRunner& operator=(const TaskRunner&) = delete;

  // Wird der Engine übergeben. Der Zeiger auf `this` steckt darin, deshalb
  // muss das Objekt die Engine überleben.
  FlutterTaskRunnerDescription GetDescription();

  // In der Hauptschleife aufrufen: führt alle fälligen Aufgaben aus.
  //
  // Der Rückgabewert ist der Zeitpunkt der nächsten fälligen Aufgabe in
  // Nanosekunden, oder 0, wenn keine ansteht – damit kann der Aufrufer seine
  // Wartezeit bemessen, statt blind zu schlafen.
  uint64_t RunExpiredTasks(FLUTTER_API_SYMBOL(FlutterEngine) engine);

  // Muss aufgerufen werden, bevor Aufgaben angenommen werden: merkt sich den
  // Thread, auf dem die Schleife läuft.
  void BindToCurrentThread();

 private:
  struct Entry {
    FlutterTask task;
    uint64_t target_time_nanos;
  };

  static void PostTaskCallback(FlutterTask task,
                               uint64_t target_time_nanos,
                               void* user_data);
  static bool RunsOnCurrentThreadCallback(void* user_data);

  bool RunsOnCurrentThread() const;
  void Enqueue(FlutterTask task, uint64_t target_time_nanos);

  std::mutex mutex_;
  std::vector<Entry> queue_;
  // libnx-Threadkennung des Schleifenthreads. Bewusst kein std::thread::id:
  // Die Engine ruft aus Threads an, die sie selbst erzeugt hat.
  uint64_t thread_id_ = 0;
  bool bound_ = false;
};

}  // namespace flutter_libnx
