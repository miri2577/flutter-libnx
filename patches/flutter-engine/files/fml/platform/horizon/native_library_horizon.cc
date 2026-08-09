// Copyright 2026 The flutter-libnx Authors.
//
// NativeLibrary für Horizon OS.
//
// Horizon Homebrew kennt kein dlopen. NROs sind eigenständige Programme, keine
// nachladbaren Bibliotheken, und einen Laufzeit-Linker gibt es nicht.
//
// Alle Operationen schlagen deshalb fehl – aber sichtbar, mit Protokolleintrag.
// Ein stiller nullptr würde die Fehlersuche an genau der Stelle erschweren, an
// der die Engine sonst ihre Snapshots sucht.
//
// Für den Flutter-Embedder ist das folgenlos, solange der AOT-Snapshot über
// Symbolreferenzen in FlutterProjectArgs übergeben wird: Dieser Weg wird in
// runtime/dart_snapshot.cc geprüft, bevor NativeLibrary überhaupt befragt wird.
// Siehe docs/feasibility.md, Abschnitt 1.

#include "flutter/fml/native_library.h"

#include "flutter/fml/logging.h"

namespace fml {

NativeLibrary::NativeLibrary(const char* path) {
  FML_LOG(ERROR) << "Dynamisches Nachladen gibt es auf Horizon nicht; "
                 << "'" << (path != nullptr ? path : "(null)")
                 << "' wurde nicht geladen.";
  handle_ = nullptr;
}

NativeLibrary::NativeLibrary(Handle handle, bool close_handle)
    : handle_(handle), close_handle_(close_handle) {}

NativeLibrary::~NativeLibrary() = default;

NativeLibrary::Handle NativeLibrary::GetHandle() const {
  return handle_;
}

fml::RefPtr<NativeLibrary> NativeLibrary::Create(const char* path) {
  auto library = fml::AdoptRef(new NativeLibrary(path));
  return library->GetHandle() != nullptr ? library : nullptr;
}

fml::RefPtr<NativeLibrary> NativeLibrary::CreateWithHandle(
    Handle handle,
    bool close_handle_when_done) {
  auto library =
      fml::AdoptRef(new NativeLibrary(handle, close_handle_when_done));
  return library->GetHandle() != nullptr ? library : nullptr;
}

fml::RefPtr<NativeLibrary> NativeLibrary::CreateForCurrentProcess() {
  // Ohne Laufzeit-Linker lassen sich auch die eigenen Symbole nicht über
  // Namen auflösen. Der Aufrufer muss Zeiger übergeben.
  FML_LOG(WARNING) << "Symbolsuche im eigenen Prozess ist auf Horizon nicht "
                      "moeglich.";
  return nullptr;
}

NativeLibrary::SymbolHandle NativeLibrary::Resolve(const char* symbol) const {
  FML_LOG(ERROR) << "Symbol '" << (symbol != nullptr ? symbol : "(null)")
                 << "' kann auf Horizon nicht zur Laufzeit aufgeloest werden.";
  return nullptr;
}

}  // namespace fml
