// Copyright 2026 The flutter-libnx Authors.
//
// OSThread für Horizon OS.
//
// libnx unterlegt den Newlib-Thread-Syscalls echte Threads, sodass pthreads
// hier vollständig zur Verfügung stehen. Die Struktur folgt deshalb eng der
// Linux-Fassung. Zwei Dinge fehlen newlib allerdings, und beide sind unten
// ausdrücklich als Lücke markiert statt stillschweigend gefüllt.

#include "platform/globals.h"

#if defined(DART_HOST_OS_HORIZON)

#include <pthread.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "platform/assert.h"
#include "platform/utils.h"
#include "vm/os_thread.h"

// Vom Embedder gestellt, damit <switch.h> nicht in die Dart-VM gerät. Fehlt
// die Datei – etwa in einem Testprogramm, das nur die VM linkt –, ist der
// Zeiger null und der Notnagel unten greift.
extern "C" __attribute__((weak)) bool flutter_libnx_get_stack_bounds(
    uintptr_t* lower,
    uintptr_t* upper);

// Dieselbe Senke, über die os_horizon.cc bereits OS::Print bedient.
extern "C" __attribute__((weak)) void flutter_libnx_vm_log(const char* text);

namespace dart {

// Ohne Auskunft des Kernels angenommene Stackgröße. Deutlich kleiner als die
// tatsächlichen Thread-Stacks (siehe GetMaxStackSize), damit die Schätzung
// innerhalb des echten Bereichs bleibt.
static const uword kFallbackStackSize = 256 * KB;

#define VALIDATE_PTHREAD_RESULT(result)                                        \
  if (result != 0) {                                                           \
    FATAL("pthread error: %d", result);                                        \
  }

const ThreadJoinId OSThread::kInvalidThreadJoinId =
    static_cast<ThreadJoinId>(nullptr);

namespace {

class ThreadStartData {
 public:
  ThreadStartData(const char* name,
                  OSThread::ThreadStartFunction function,
                  uword parameter)
      : name_(name), function_(function), parameter_(parameter) {}

  const char* name() const { return name_; }
  OSThread::ThreadStartFunction function() const { return function_; }
  uword parameter() const { return parameter_; }

 private:
  const char* name_;
  OSThread::ThreadStartFunction function_;
  uword parameter_;

  DISALLOW_COPY_AND_ASSIGN(ThreadStartData);
};

void* ThreadStart(void* data_ptr) {
  ThreadStartData* data = reinterpret_cast<ThreadStartData*>(data_ptr);

  const char* name = data->name();
  OSThread::ThreadStartFunction function = data->function();
  uword parameter = data->parameter();
  delete data;

  // Der neue Thread braucht ein eigenes OSThread-Objekt in seinem TLS, bevor
  // irgendetwas anderes läuft.
  OSThread* thread = OSThread::CreateOSThread();
  if (thread != nullptr) {
    OSThread::SetCurrent(thread);
    thread->SetName(name);
    function(parameter);
  }
  return nullptr;
}

}  // namespace

int OSThread::TryStart(const char* name,
                       ThreadStartFunction function,
                       uword parameter) {
  pthread_attr_t attr;
  int result = pthread_attr_init(&attr);
  if (result != 0) {
    return result;
  }

  // Horizon verlangt 4K-ausgerichtete Thread-Stacks; die Newlib-Anbindung von
  // libnx prüft das und lehnt andernfalls ab. Dart liefert bereits einen
  // gerundeten Wert, wir runden zur Sicherheit erneut auf.
  const intptr_t stack_size =
      Utils::RoundUp(OSThread::GetMaxStackSize(), 4096);
  result = pthread_attr_setstacksize(&attr, stack_size);
  if (result != 0) {
    pthread_attr_destroy(&attr);
    return result;
  }

  ThreadStartData* data = new ThreadStartData(name, function, parameter);

  pthread_t tid;
  result = pthread_create(&tid, &attr, ThreadStart, data);
  if (result != 0) {
    delete data;
    pthread_attr_destroy(&attr);
    return result;
  }

  result = pthread_attr_destroy(&attr);
  VALIDATE_PTHREAD_RESULT(result);
  return 0;
}

intptr_t OSThread::GetMaxStackSize() {
  // Derselbe Wert wie unter Linux: 128 Wörter mal Kilobyte, auf 64 Bit also
  // 1 MB. Horizon-Threads bekommen ihren Stack aus dem Prozess-Heap, eine
  // systemseitige Obergrenze gibt es nicht.
  const int kStackSize = (128 * kWordSize * KB);
  return kStackSize;
}

ThreadId OSThread::GetCurrentThreadTraceId() {
  // Linux liefert hier die Kernel-TID. Horizon hat kein Gegenstück, das ohne
  // <switch.h> erreichbar wäre; die pthread-Kennung ist innerhalb des
  // Prozesses ebenfalls eindeutig und erfüllt den Zweck der Ablaufverfolgung.
  return pthread_self();
}

char* OSThread::GetCurrentThreadName() {
  // newlib kennt kein pthread_getname_np. Die VM führt den bei der Erzeugung
  // gesetzten Namen ohnehin selbst mit (OSThread::name()); hier gibt es nur
  // keinen zusätzlichen Namen auf Betriebssystemebene.
  char* name = static_cast<char*>(malloc(1));
  if (name != nullptr) {
    name[0] = '\0';
  }
  return name;
}

ThreadJoinId OSThread::GetCurrentThreadJoinId(OSThread* thread) {
  ASSERT(thread != nullptr);
  return pthread_self();
}

void OSThread::Join(ThreadJoinId id) {
  int result = pthread_join(id, nullptr);
  ASSERT(result == 0);
}

void OSThread::Detach(ThreadJoinId id) {
  int result = pthread_detach(id);
  VALIDATE_PTHREAD_RESULT(result);
}

intptr_t OSThread::ThreadIdToIntPtr(ThreadId id) {
  ASSERT(sizeof(id) <= sizeof(intptr_t));
  return reinterpret_cast<intptr_t>(id);
}

bool OSThread::GetCurrentStackBounds(uword* lower, uword* upper) {
  // Linux ermittelt die Grenzen über pthread_getattr_np; newlib bietet das
  // nicht. Der Kernel weiß es trotzdem: svcQueryMemory liefert zu einer
  // Adresse die umgebende Speicherregion, und für Haupt- wie Nebenthreads ist
  // das genau der Stack. Der Aufruf liegt im Embedder
  // (embedder/src/platform/stack_bounds_horizon.cpp), weil <switch.h> hier
  // nicht hineindarf – dasselbe Vorgehen wie bei der Logsenke.
  //
  // Ein Rückgabewert false ist kein weicher Rückfall: os_thread.cc macht
  // daraus FATAL("Failed to retrieve stack bounds"). Deshalb gibt es unten
  // einen Notnagel statt eines Abbruchs.
  if (flutter_libnx_get_stack_bounds != nullptr) {
    uintptr_t region_lower = 0;
    uintptr_t region_upper = 0;
    if (flutter_libnx_get_stack_bounds(&region_lower, &region_upper)) {
      *lower = static_cast<uword>(region_lower);
      *upper = static_cast<uword>(region_upper);
      return true;
    }
  }

  // Notnagel. Er rät, und zwar bewusst in die sichere Richtung: Eine zu hoch
  // angesetzte Untergrenze lässt die VM einen Stapelüberlauf zu früh melden –
  // als kontrollierte Dart-Ausnahme. Eine zu niedrige würde ihn übersehen und
  // in einem echten Speicherzugriffsfehler enden.
  if (flutter_libnx_vm_log != nullptr) {
    flutter_libnx_vm_log(
        "OSThread::GetCurrentStackBounds: svcQueryMemory nicht verfuegbar, "
        "konservative Schaetzung wird benutzt\n");
  }
  const uword sp = GetCurrentStackPointer();
  const uword top = Utils::RoundUp(sp + 1, 4096);
  *upper = top;
  *lower = top - kFallbackStackSize;
  return true;
}

// GetCurrentStackPointer() steht bereits in os_thread.cc und ist
// plattformunabhaengig - hier definiert, gaebe es ein doppeltes Symbol.

ThreadLocalKey OSThread::CreateThreadLocal(ThreadDestructor destructor) {
  pthread_key_t key = kUnsetThreadLocalKey;
  int result = pthread_key_create(&key, destructor);
  VALIDATE_PTHREAD_RESULT(result);
  ASSERT(key != kUnsetThreadLocalKey);
  return key;
}

void OSThread::SetThreadLocal(ThreadLocalKey key, uword value) {
  ASSERT(key != kUnsetThreadLocalKey);
  int result = pthread_setspecific(key, reinterpret_cast<void*>(value));
  VALIDATE_PTHREAD_RESULT(result);
}

}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
