// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Dateisystemueberwachung gibt es auf Horizon nicht.
//
// Die Linux-Fassung baut auf inotify auf; der Dateidienst der Switch kennt
// nichts Vergleichbares, und ein Nachbau durch regelmaessiges Abtasten waere
// etwas anderes als das, was die Schnittstelle verspricht - Ereignisse kaemen
// verspaetet, kurzlebige Aenderungen gar nicht.
//
// dart:io ist auf genau diesen Fall vorbereitet: `IsSupported()` gibt es,
// damit eine Plattform sich abmelden kann. Die Dart-Seite wirft dann eine
// aussagekraeftige Ausnahme, statt auf Ereignisse zu warten, die nie kommen.
// Das ist der vorgesehene Weg und nicht eine Notloesung.

#include "platform/globals.h"
#if defined(DART_HOST_OS_HORIZON)

#include <errno.h>

#include "bin/file_system_watcher.h"

#include "bin/dartutils.h"

namespace dart {
namespace bin {

bool FileSystemWatcher::IsSupported() {
  return false;
}

void FileSystemWatcher::InitOnce() {}

void FileSystemWatcher::Cleanup() {}

intptr_t FileSystemWatcher::Init() {
  // Wird nur erreicht, wenn jemand IsSupported() uebergeht.
  errno = ENOSYS;
  return -1;
}

void FileSystemWatcher::Close(intptr_t id) {}

intptr_t FileSystemWatcher::WatchPath(intptr_t id,
                                      Namespace* namespc,
                                      const char* path,
                                      int events,
                                      bool recursive) {
  errno = ENOSYS;
  return -1;
}

void FileSystemWatcher::UnwatchPath(intptr_t id, intptr_t path_id) {}

intptr_t FileSystemWatcher::GetSocketId(intptr_t id, intptr_t path_id) {
  errno = ENOSYS;
  return -1;
}

Dart_Handle FileSystemWatcher::ReadEvents(intptr_t id, intptr_t path_id) {
  return DartUtils::NewDartOSError();
}

}  // namespace bin
}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
