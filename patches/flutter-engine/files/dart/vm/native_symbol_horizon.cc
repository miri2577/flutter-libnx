// Copyright 2026 The flutter-libnx Authors.
//
// NativeSymbolResolver für Horizon OS.
//
// Die Auflösung von Adressen zu Symbolnamen setzt sonst einen Laufzeit-Linker
// voraus (dladdr) oder das Auslesen der eigenen ELF-Symboltabelle. Horizon
// Homebrew bietet weder das eine noch das andere: Eine NRO ist ein
// eigenständiges Programm ohne dynamische Symbolauflösung.
//
// Alle Anfragen melden deshalb "nicht gefunden". Das ist folgenlos für die
// Ausführung – betroffen sind ausschließlich Stack-Traces und Profilausgaben,
// die auf dieser Plattform ohnehin nicht verfügbar sind (vgl.
// fml/backtrace.cc, das aus demselben Grund null Frames liefert).

#include "platform/globals.h"

#if defined(DART_HOST_OS_HORIZON)

#include <cstdlib>

#include "vm/native_symbol.h"

namespace dart {

void NativeSymbolResolver::Init() {}

void NativeSymbolResolver::Cleanup() {}

const char* NativeSymbolResolver::LookupSymbolName(uword pc, uword* start) {
  if (start != nullptr) {
    *start = 0;
  }
  return nullptr;
}

bool NativeSymbolResolver::LookupSharedObject(uword pc,
                                              uword* dso_base,
                                              const char** dso_name) {
  return false;
}

void NativeSymbolResolver::FreeSymbolName(const char* name) {
  free(const_cast<char*>(name));
}

}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
