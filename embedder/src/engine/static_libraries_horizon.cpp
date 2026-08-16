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

#define MPV_SYMBOL(name) extern "C" void name(void);
#include "../../../third_party/mpv/mpv_symbols.inc"
#undef MPV_SYMBOL

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

// libmpv (devkitPro-Portlib) für media_kit - dieselbe Mechanik.
const StaticSymbol kMpvSymbols[] = {
#define MPV_SYMBOL(name) {#name, reinterpret_cast<void*>(&name)},
#include "../../../third_party/mpv/mpv_symbols.inc"
#undef MPV_SYMBOL
};

constexpr int kMpvSymbolCount =
    static_cast<int>(sizeof(kMpvSymbols) / sizeof(kMpvSymbols[0]));

int g_mpv_marker = 0;

// DIAGNOSE (Instruction Abort in mpv_set_wakeup_callback): Der Shim
// protokolliert den von Dart gelieferten Callback-Zeiger, BEVOR mpv ihn
// anspringt. Liegt er im Modul (.text des Snapshots), sind die
// Snapshot-Trampoline aktiv; liegt er im Heap, greift der
// Fuchsia-Trampolin-Zweig nicht. Nach der Fehlersuche ausbauen.
// Aufruf des Originals ueber die Tabellenadresse - eine Zweitdeklaration
// wuerde mit der Dummy-Signatur aus der .inc kollidieren.
void* LookupMpvRaw(const char* name);

extern "C" void flutter_libnx_mpv_wakeup_shim(void* ctx,
                                              void (*cb)(void*),
                                              void* d) {
  LOG_INFO("mpv wakeup-callback von Dart: %p", reinterpret_cast<void*>(cb));
  using SetWakeupFn = void (*)(void*, void (*)(void*), void*);
  void* real = LookupMpvRaw("mpv_set_wakeup_callback");
  if (real != nullptr) {
    reinterpret_cast<SetWakeupFn>(real)(ctx, cb, d);
  }
}

void* LookupMpvRaw(const char* name) {
  int lo = 0;
  int hi = kMpvSymbolCount - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    const int cmp = strcmp(kMpvSymbols[mid].name, name);
    if (cmp == 0) {
      return kMpvSymbols[mid].address;
    }
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

void* LookupMpv(const char* name) {
  if (strcmp(name, "mpv_set_wakeup_callback") == 0) {
    return reinterpret_cast<void*>(&flutter_libnx_mpv_wakeup_shim);
  }
  return LookupMpvRaw(name);
}

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

  // libmpv.so, libmpv.so.2, mpv.so - media_kit probiert mehrere Namen.
  if (strncmp(base, "libmpv.so", 9) == 0 || strncmp(base, "mpv.so", 6) == 0) {
    LOG_INFO("dl_open('%s') -> statisch gelinktes libmpv %s", path,
             kMpvSymbolCount > 0 ? "" : "(LEERE Tabelle!)");
    return &g_mpv_marker;
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
  if (handle == &g_mpv_marker) {
    return LookupMpv(name);
  }
  // Prozess-Handle (nullptr von DynamicLibrary.process()/executable() und
  // vom Prozess-Fallback der native assets): alle Tabellen durchsuchen.
  if (handle == nullptr) {
    void* process_symbol = LookupProcess(name);
    if (process_symbol != nullptr) {
      return process_symbol;
    }
    void* sqlite_symbol = LookupSqlite3(name);
    if (sqlite_symbol != nullptr) {
      return sqlite_symbol;
    }
    return LookupMpv(name);
  }
  return nullptr;
}
