// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Prozesse gibt es auf Horizon nicht - jedenfalls nicht so, wie dart:io sie
// meint.
//
// Die Linux-Fassung dieser Datei hat 1203 Zeilen, und praktisch alles darin
// steht auf fork/exec, Pipes zwischen Eltern und Kind, waitpid und
// Signalzustellung. Horizon hat davon nichts: Ein Homebrew-Titel laeuft
// allein, kann keine Kindprozesse erzeugen und bekommt keine Signale. Es gibt
// hier also nichts zu portieren, nur etwas ehrlich abzulehnen.
//
// Der Unterschied zwischen "ehrlich ablehnen" und "stillschweigend nichts
// tun" ist der ganze Punkt dieser Datei: Jede Funktion, die scheitern muss,
// scheitert sichtbar und mit passendem errno. Was ohne Prozesse trotzdem
// sinnvoll ist - der globale Exitcode, der Exit-Hook -, funktioniert
// vollstaendig, denn das braucht die Laufzeit auch ohne Kindprozesse.

#include "platform/globals.h"
#if defined(DART_HOST_OS_HORIZON)

#include <errno.h>
#include <string.h>

#include "bin/process.h"

#include "platform/utils.h"

namespace dart {
namespace bin {

int Process::global_exit_code_ = 0;
Mutex* Process::global_exit_code_mutex_ = nullptr;
Process::ExitHook Process::exit_hook_ = nullptr;

// Legt eine Fehlermeldung im von dart:io erwarteten Format ab. Der Aufrufer
// uebernimmt den Speicher.
static void SetErrorMessage(char** os_error_message, const char* message) {
  if (os_error_message == nullptr) {
    return;
  }
  *os_error_message = Utils::StrDup(message);
}

int Process::Start(Namespace* namespc,
                   const char* path,
                   const char* arguments[],
                   intptr_t arguments_length,
                   const char* working_directory,
                   char* environment[],
                   intptr_t environment_length,
                   ProcessStartMode mode,
                   intptr_t* in,
                   intptr_t* out,
                   intptr_t* err,
                   intptr_t* id,
                   intptr_t* exit_handler,
                   char** os_error_message) {
  SetErrorMessage(os_error_message,
                  "Horizon kann keine Kindprozesse starten");
  errno = ENOSYS;
  return errno;
}

int Process::Exec(Namespace* namespc,
                  const char* path,
                  const char* arguments[],
                  intptr_t arguments_length,
                  const char* working_directory,
                  char* errmsg,
                  intptr_t errmsg_len) {
  snprintf(errmsg, errmsg_len, "%s",
           "Horizon kann das Prozessabbild nicht ersetzen");
  errno = ENOSYS;
  return -1;
}

bool Process::Wait(intptr_t id,
                   intptr_t in,
                   intptr_t out,
                   intptr_t err,
                   intptr_t exit_handler,
                   ProcessResult* result) {
  // Kann nur erreicht werden, wenn Start entgegen seiner Rueckmeldung als
  // erfolgreich behandelt wurde.
  errno = ECHILD;
  return false;
}

bool Process::Kill(intptr_t id, int signal) {
  errno = ESRCH;
  return false;
}

void Process::TerminateExitCodeHandler() {
  // Es laeuft kein Handler-Thread, weil nie ein Kindprozess entstanden ist.
}

intptr_t Process::CurrentProcessId() {
  // Horizon vergibt Prozess-IDs, aber libnx macht sie nicht zugaenglich, und
  // dart:io benutzt den Wert ohnehin nur zur Anzeige. 1 ist die uebliche
  // Antwort fuer "der einzige Prozess".
  return 1;
}

int64_t Process::CurrentRSS() {
  // Waere ueber svcGetInfo(InfoType_UsedMemorySize) zu haben, dafuer muesste
  // aber <switch.h> in den Dart-Baum - und dessen Makros beissen sich mit
  // Dart-Bezeichnern, siehe os_horizon.cc. -1 ist der von dart:io
  // vorgesehene Wert fuer "nicht ermittelbar".
  return -1;
}

int64_t Process::MaxRSS() {
  return -1;
}

intptr_t Process::SetSignalHandler(intptr_t signal) {
  errno = ENOSYS;
  return -1;
}

void Process::ClearSignalHandler(intptr_t signal, Dart_Port port) {
  // Es kann keinen geben, also gibt es nichts zu loeschen.
}

void Process::ClearSignalHandlerByFd(intptr_t fd, Dart_Port port) {}

void Process::ClearAllSignalHandlers() {}

void Process::Init() {
  ASSERT(Process::global_exit_code_mutex_ == nullptr);
  Process::global_exit_code_mutex_ = new Mutex();
}

void Process::Cleanup() {
  ASSERT(Process::global_exit_code_mutex_ != nullptr);
  delete Process::global_exit_code_mutex_;
  Process::global_exit_code_mutex_ = nullptr;
}

}  // namespace bin
}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
