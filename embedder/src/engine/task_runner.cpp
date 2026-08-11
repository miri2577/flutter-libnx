#include "flutter_libnx/task_runner.h"

#include <algorithm>

#include <switch.h>

#include "flutter_libnx/log.h"

namespace flutter_libnx {

namespace {

// Threadkennung des aufrufenden Threads.
//
// `threadGetCurHandle()` liefert das Kernel-Handle des laufenden Threads und
// ist damit innerhalb des Prozesses eindeutig. pthread_self() täte es auch,
// aber die Engine ruft aus Threads an, die libnx erzeugt hat – das Handle ist
// die Auskunft, die dort in jedem Fall stimmt.
uint64_t CurrentThreadId() {
  return static_cast<uint64_t>(threadGetCurHandle());
}

}  // namespace

TaskRunner::TaskRunner() = default;

void TaskRunner::BindToCurrentThread() {
  std::lock_guard<std::mutex> lock(mutex_);
  thread_id_ = CurrentThreadId();
  bound_ = true;
}

bool TaskRunner::RunsOnCurrentThread() const {
  return bound_ && CurrentThreadId() == thread_id_;
}

void TaskRunner::Enqueue(FlutterTask task, uint64_t target_time_nanos) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push_back(Entry{task, target_time_nanos});
}

// static
void TaskRunner::PostTaskCallback(FlutterTask task,
                                  uint64_t target_time_nanos,
                                  void* user_data) {
  static_cast<TaskRunner*>(user_data)->Enqueue(task, target_time_nanos);
}

// static
bool TaskRunner::RunsOnCurrentThreadCallback(void* user_data) {
  return static_cast<TaskRunner*>(user_data)->RunsOnCurrentThread();
}

FlutterTaskRunnerDescription TaskRunner::GetDescription() {
  FlutterTaskRunnerDescription description = {};
  description.struct_size = sizeof(FlutterTaskRunnerDescription);
  description.user_data = this;
  description.runs_task_on_current_thread_callback =
      &TaskRunner::RunsOnCurrentThreadCallback;
  description.post_task_callback = &TaskRunner::PostTaskCallback;
  // Die Kennung muss über die Laufzeit stabil sein; eine feste Zahl genügt,
  // weil es genau einen Runner gibt.
  description.identifier = 1;
  return description;
}

uint64_t TaskRunner::RunExpiredTasks(
    FLUTTER_API_SYMBOL(FlutterEngine) engine) {
  if (engine == nullptr) {
    return 0;
  }

  const uint64_t now = FlutterEngineGetCurrentTime();

  // Fällige Aufgaben herausnehmen, bevor sie ausgeführt werden: Eine Aufgabe
  // darf während ihrer Ausführung neue einreihen, und die Sperre darf dabei
  // nicht gehalten werden – sonst blockiert der erste Rückruf, der aus
  // demselben Thread etwas nachreicht.
  std::vector<Entry> due;
  uint64_t next_time = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Entry> remaining;
    remaining.reserve(queue_.size());
    for (const Entry& entry : queue_) {
      if (entry.target_time_nanos <= now) {
        due.push_back(entry);
      } else {
        remaining.push_back(entry);
        if (next_time == 0 || entry.target_time_nanos < next_time) {
          next_time = entry.target_time_nanos;
        }
      }
    }
    queue_.swap(remaining);
  }

  for (const Entry& entry : due) {
    const FlutterEngineResult result = FlutterEngineRunTask(engine, &entry.task);
    if (result != kSuccess) {
      LOG_ERROR("FlutterEngineRunTask fehlgeschlagen: %d",
                static_cast<int>(result));
    }
  }

  return next_time;
}

}  // namespace flutter_libnx
