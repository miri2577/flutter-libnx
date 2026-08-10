// Copyright 2026 The flutter-libnx Authors.
//
// Stdin/Stdout für Horizon OS.
//
// Die Linux-Fassung besteht fast vollständig aus Terminalsteuerung über
// termios: Echo an- und abschalten, Zeilenmodus, Fenstergröße. Auf der Switch
// gibt es kein Terminal – newlib bringt zwar einen termios-Header mit, dessen
// eigener Include `sys/termios.h` fehlt aber, was die Lage treffend beschreibt.
//
// Alle Abfragen melden deshalb `false` (nicht unterstützt). Dart wertet das
// aus: `stdin.echoMode` und Verwandte werfen dann eine Ausnahme, statt einen
// erfundenen Zustand zu liefern.
//
// Lesen und Schreiben selbst funktionieren – stdout landet je nach
// Konfiguration auf der Konsole, über nxlink oder in der Logdatei.

#include "platform/globals.h"
#if defined(DART_HOST_OS_HORIZON)

#include "bin/stdio.h"

#include <errno.h>   // NOLINT
#include <unistd.h>  // NOLINT

#include "bin/fdutils.h"
#include "platform/signal_blocker.h"

namespace dart {
namespace bin {

bool Stdin::ReadByte(intptr_t fd, int* byte) {
  unsigned char b;
  ssize_t s = TEMP_FAILURE_RETRY(read(fd, &b, 1));
  if (s < 0) {
    return false;
  }
  *byte = (s == 0) ? -1 : b;
  return true;
}

bool Stdin::GetEchoMode(intptr_t fd, bool* enabled) {
  return false;
}

bool Stdin::SetEchoMode(intptr_t fd, bool enabled) {
  return false;
}

bool Stdin::GetEchoNewlineMode(intptr_t fd, bool* enabled) {
  return false;
}

bool Stdin::SetEchoNewlineMode(intptr_t fd, bool enabled) {
  return false;
}

bool Stdin::GetLineMode(intptr_t fd, bool* enabled) {
  return false;
}

bool Stdin::SetLineMode(intptr_t fd, bool enabled) {
  return false;
}

bool Stdin::AnsiSupported(intptr_t fd, bool* supported) {
  // Die Konsole von libnx wertet keine ANSI-Sequenzen aus.
  *supported = false;
  return true;
}

bool Stdout::GetTerminalSize(intptr_t fd, int size[2]) {
  return false;
}

bool Stdout::AnsiSupported(intptr_t fd, bool* supported) {
  *supported = false;
  return true;
}

}  // namespace bin
}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
