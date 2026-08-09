// Copyright 2026 The flutter-libnx Authors.
//
// Thread-Interna für Horizon OS.
//
// Inhaltlich identisch zur Linux-Fassung, weil libnx über die
// Newlib-Syscalls echte pthreads bereitstellt (siehe
// libnx/nx/source/runtime/newlib.c: __syscall_thread_create und Verwandte).
// Eigene Datei statt Wiederverwendung von os_thread_linux.h, damit die
// Unterschiede – falls welche auftreten – an einer Stelle sichtbar bleiben
// und nicht über DART_HOST_OS_LINUX mitgeschleift werden.

#ifndef RUNTIME_VM_OS_THREAD_HORIZON_H_
#define RUNTIME_VM_OS_THREAD_HORIZON_H_

#if !defined(RUNTIME_VM_OS_THREAD_H_)
#error Do not include os_thread_horizon.h directly; use os_thread.h instead.
#endif

#include <pthread.h>

#include "platform/assert.h"
#include "platform/globals.h"

namespace dart {

typedef pthread_key_t ThreadLocalKey;
typedef pthread_t ThreadJoinId;

static const ThreadLocalKey kUnsetThreadLocalKey =
    static_cast<pthread_key_t>(-1);

class ThreadInlineImpl {
 private:
  ThreadInlineImpl() {}
  ~ThreadInlineImpl() {}

  static uword GetThreadLocal(ThreadLocalKey key) {
    ASSERT(key != kUnsetThreadLocalKey);
    return reinterpret_cast<uword>(pthread_getspecific(key));
  }

  friend class OSThread;

  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(ThreadInlineImpl);
};

}  // namespace dart

#endif  // RUNTIME_VM_OS_THREAD_HORIZON_H_
