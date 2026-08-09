// Copyright 2026 The flutter-libnx Authors.
//
// OS-Abstraktion der Dart-VM für Horizon OS.
//
// Bewusst ohne <switch.h>: Dessen Makros und Typnamen (u32, Result, ...)
// kollidieren mit Dart-eigenen Bezeichnern. Alles hier Benötigte gibt es über
// die POSIX-Schnittstellen, die libnx den Newlib-Syscalls unterlegt.

#include "platform/globals.h"

#if defined(DART_HOST_OS_HORIZON)

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

#include "platform/assert.h"
#include "platform/utils.h"
#include "vm/os.h"
#include "vm/zone.h"

namespace dart {

intptr_t OS::ProcessId() {
  // Horizon-Homebrew läuft als einzelner Prozess ohne für uns sichtbare PID.
  // Der Wert dient in der VM nur der Protokollierung.
  return 0;
}

int64_t OS::GetCurrentTimeMillis() {
  return GetCurrentTimeMicros() / 1000;
}

int64_t OS::GetCurrentTimeMicros() {
  struct timespec ts = {};
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (static_cast<int64_t>(ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

int64_t OS::GetCurrentMonotonicMicros() {
  struct timespec ts = {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (static_cast<int64_t>(ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}

int64_t OS::GetCurrentMonotonicMicrosForTimeline() {
  return GetCurrentMonotonicMicros();
}

int64_t OS::GetCurrentThreadCPUMicros() {
  // CLOCK_THREAD_CPUTIME_ID bietet newlib auf dieser Plattform nicht. Die VM
  // wertet -1 als "nicht verfügbar" aus; ein erfundener Wert wäre schlechter
  // als keiner.
  return -1;
}

void OS::Sleep(int64_t millis) {
  struct timespec req = {};
  req.tv_sec = static_cast<time_t>(millis / 1000);
  req.tv_nsec = static_cast<long>((millis % 1000) * 1000000);
  nanosleep(&req, nullptr);
}

void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);
  fflush(stdout);
}

void OS::PrintErr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fflush(stderr);
}

char* OS::VSCreate(Zone* zone, const char* format, va_list args) {
  // Erst die Länge ermitteln, dann einmal formatieren. va_list darf nur einmal
  // durchlaufen werden, deshalb zwei Kopien.
  va_list measure_args;
  va_copy(measure_args, args);
  intptr_t len = Utils::VSNPrint(nullptr, 0, format, measure_args);
  va_end(measure_args);

  char* buffer;
  if (zone != nullptr) {
    buffer = zone->Alloc<char>(len + 1);
  } else {
    buffer = reinterpret_cast<char*>(malloc(len + 1));
  }
  ASSERT(buffer != nullptr);

  va_list print_args;
  va_copy(print_args, args);
  Utils::VSNPrint(buffer, len + 1, format, print_args);
  va_end(print_args);
  return buffer;
}

char* OS::SCreate(Zone* zone, const char* format, ...) {
  va_list args;
  va_start(args, format);
  char* buffer = VSCreate(zone, format, args);
  va_end(args);
  return buffer;
}

bool OS::ParseInitialInt64(const char* str, int64_t* value, char** end) {
  ASSERT(str != nullptr && strlen(str) > 0 && value != nullptr &&
         end != nullptr);
  int32_t base = 10;
  int i = 0;
  if (str[i] == '-' || str[i] == '+') {
    i++;
  }
  if ((str[i] == '0') && ((str[i + 1] == 'x') || (str[i + 1] == 'X')) &&
      (str[i + 2] != '\0')) {
    base = 16;
  }
  errno = 0;
  *value = strtoll(str, end, base);
  return (errno == 0) && (*end != str);
}

void OS::Init() {}

void OS::Cleanup() {}

void OS::Abort() {
  abort();
}

OS::BuildId OS::GetAppBuildId(const uint8_t* snapshot_instructions) {
  // Build-IDs stammen sonst aus dem ELF-Notes-Abschnitt des geladenen Moduls.
  // In einer NRO ist dieser Weg nicht zugänglich; die VM kommt ohne aus und
  // verzichtet dann auf die Zuordnung von Symbolen zu Snapshots.
  return {0, nullptr};
}

}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
