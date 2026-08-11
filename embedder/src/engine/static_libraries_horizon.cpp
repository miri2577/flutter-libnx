// FFI ohne dlopen: statisch gelinkte C-Bibliotheken für Dart sichtbar machen.
//
// Horizon hat keinen Laufzeit-Linker. `DynamicLibrary.open()` und die
// Symbolauflösung der Dart-VM laufen deshalb über zwei schwach gebundene
// Haken (`flutter_libnx_dl_open`/`_dl_sym`, Patch
// patch_dart_platform_utils_dynlib_hooks in patch-engine-horizon.py), die
// dieser Embedder mit einer Tabelle statisch gelinkter Symbole bedient.
// Fehlt der Embedder oder das Symbol, bleibt es bei der ehrlichen
// Fehlermeldung in der VM.
//
// Erster Kunde ist sqlite3 (third_party/sqlite3, gebaut von
// scripts/build-sqlite3.sh): package:sqlite3 ruft
// DynamicLibrary.open('libsqlite3.so') und löst dann rund 100
// sqlite3_*-Funktionen auf. Die Tabelle kommt aus der beim Bau erzeugten
// sqlite3_symbols.inc - sie ist zugleich der Anker, der den Linker zwingt,
// die Bibliothek überhaupt in die NRO aufzunehmen (nichts anderes im
// Programm referenziert sqlite3_*).
//
// Dateisperren: Beim ersten open() wird "unix-none" (keine Sperren) zum
// Standard-VFS erklärt. Der Unix-VFS von sqlite verlangt sonst
// fcntl-Sperren, die devoptab nicht kennt - und auf einer Konsole greift
// ohnehin kein zweiter Prozess auf die Datenbank zu.

#include <stdlib.h>
#include <string.h>

#include "flutter_libnx/log.h"

// Die Deklarationen brauchen nur die Namen, keine echten Signaturen: Es geht
// ausschließlich um Adressen, aufgerufen wird über Darts FFI-Typisierung.
#define SQLITE3_SYMBOL(name) extern "C" void name(void);
#include "../../../third_party/sqlite3/sqlite3_symbols.inc"
#undef SQLITE3_SYMBOL

namespace {

struct StaticSymbol {
  const char* name;
  void* address;
};

// Von build-sqlite3.sh alphabetisch sortiert erzeugt (sort -u) - die
// binäre Suche unten verlässt sich darauf.
const StaticSymbol kSqlite3Symbols[] = {
#define SQLITE3_SYMBOL(name) {#name, reinterpret_cast<void*>(&name)},
#include "../../../third_party/sqlite3/sqlite3_symbols.inc"
#undef SQLITE3_SYMBOL
};

constexpr int kSqlite3SymbolCount =
    static_cast<int>(sizeof(kSqlite3Symbols) / sizeof(kSqlite3Symbols[0]));

// Ein beliebiges eindeutiges Handle; nur die Adresse zählt.
int g_sqlite3_marker = 0;

// Prozessweite Symbole für DynamicLibrary.process(): package:ffi löst
// seine Allokatoren über den Prozess auf (malloc/free bzw. calloc).
// Alphabetisch, wie die sqlite3-Tabelle.
const StaticSymbol kProcessSymbols[] = {
    {"calloc", reinterpret_cast<void*>(&calloc)},
    {"free", reinterpret_cast<void*>(&free)},
    {"malloc", reinterpret_cast<void*>(&malloc)},
    {"realloc", reinterpret_cast<void*>(&realloc)},
};

constexpr int kProcessSymbolCount =
    static_cast<int>(sizeof(kProcessSymbols) / sizeof(kProcessSymbols[0]));

void* LookupProcess(const char* name) {
  for (int i = 0; i < kProcessSymbolCount; i++) {
    if (strcmp(kProcessSymbols[i].name, name) == 0) {
      return kProcessSymbols[i].address;
    }
  }
  return nullptr;
}

void* LookupSqlite3(const char* name) {
  int lo = 0;
  int hi = kSqlite3SymbolCount - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const int cmp = strcmp(kSqlite3Symbols[mid].name, name);
    if (cmp == 0) {
      return kSqlite3Symbols[mid].address;
    }
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

// "unix-none" als Standard-VFS registrieren, über die eigene Tabelle statt
// über Prototypen - die Deklarationen oben haben absichtlich falsche
// Signaturen, ein direkter Aufruf wäre undefiniert.
void EnsureLocklessDefaultVfs() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  using VfsFindFn = void* (*)(const char*);
  using VfsRegisterFn = int (*)(void*, int);
  auto find = reinterpret_cast<VfsFindFn>(LookupSqlite3("sqlite3_vfs_find"));
  auto reg =
      reinterpret_cast<VfsRegisterFn>(LookupSqlite3("sqlite3_vfs_register"));
  if (find == nullptr || reg == nullptr) {
    LOG_ERROR("sqlite3: vfs_find/vfs_register fehlen in der Symboltabelle");
    return;
  }
  void* none = find("unix-none");
  if (none == nullptr) {
    LOG_ERROR("sqlite3: VFS 'unix-none' nicht gefunden");
    return;
  }
  reg(none, /*makeDflt=*/1);
  LOG_INFO("sqlite3: 'unix-none' (ohne Dateisperren) ist Standard-VFS");
}

}  // namespace

extern "C" void* flutter_libnx_dl_open(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
  const char* base = strrchr(path, '/');
  base = (base != nullptr) ? base + 1 : path;

  // libsqlite3.so, libsqlite3.so.0, sqlite3.so - alles, was package:sqlite3
  // und Verwandte je probieren.
  if (strncmp(base, "libsqlite3.so", 13) == 0 ||
      strncmp(base, "sqlite3.so", 10) == 0) {
    EnsureLocklessDefaultVfs();
    LOG_INFO("dl_open('%s') -> statisch gelinktes sqlite3", path);
    return &g_sqlite3_marker;
  }

  LOG_WARN("dl_open('%s'): nicht statisch gelinkt", path);
  return nullptr;
}

extern "C" void* flutter_libnx_dl_sym(void* handle, const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  if (handle == &g_sqlite3_marker) {
    return LookupSqlite3(name);
  }
  // Prozess-Handle (nullptr von DynamicLibrary.process()/executable() und
  // vom Prozess-Fallback der native assets): alle Tabellen durchsuchen.
  if (handle == nullptr) {
    void* process_symbol = LookupProcess(name);
    if (process_symbol != nullptr) {
      return process_symbol;
    }
    return LookupSqlite3(name);
  }
  return nullptr;
}
