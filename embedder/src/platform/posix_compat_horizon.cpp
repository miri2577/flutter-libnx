// Verzeichnis-Deskriptoren und *at-Funktionen für Horizon.
//
// Warum es diese Datei gibt
// -------------------------
// Die Datei-API von fml (`flutter/fml/platform/posix/file_posix.cc`) ist
// durchgängig fd-relativ gebaut: Ein Verzeichnis wird einmal geöffnet, und
// alles Weitere läuft über `openat`, `mkdirat`, `unlinkat`, `renameat`,
// `faccessat` und `fdopendir` relativ zu diesem Deskriptor. Das ist auf
// POSIX-Systemen der übliche und sichere Weg.
//
// Horizon kennt dieses Modell nicht. libnx reicht Dateizugriffe über
// devoptab weiter, und dessen Schnittstelle (`sys/iosupport.h`) trennt beide
// Welten strikt: `open_r` liefert einen Dateideskriptor, `diropen_r` einen
// `DIR_ITER*`. Es gibt keine Brücke zwischen beiden. Ein Deskriptor auf ein
// Verzeichnis ist auf dieser Plattform nicht bloß unimplementiert, sondern
// im Datenmodell nicht vorgesehen. Entsprechend definiert newlib zwar alle
// *at-Funktionen in den Headern, aber keine davon in der Bibliothek.
//
// Wie die Lücke geschlossen wird
// ------------------------------
// Verzeichnisse bekommen Handles aus einem eigenen Zahlenbereich oberhalb
// aller echten Deskriptoren. Zu jedem Handle merkt sich diese Datei den
// vollständigen Pfad. Die *at-Funktionen setzen daraus wieder einen Pfad
// zusammen und rufen die pfadbasierte Variante auf, die libnx anbietet.
//
// Weil `dup`, `close` und `fstat` ebenfalls auf solche Handles treffen,
// werden sie über `-Wl,--wrap=...` abgefangen. Für echte Deskriptoren
// reichen die Wrapper unverändert an libc weiter; der Aufpreis ist ein
// Integervergleich.
//
// Was das kostet
// --------------
// Der Schutz, den fd-relative Zugriffe eigentlich bieten – dass das
// Basisverzeichnis zwischen Öffnen und Benutzen nicht ausgetauscht werden
// kann – geht verloren. Auf einer Konsole ohne Mehrbenutzerbetrieb und ohne
// fremde Prozesse im selben Dateisystem ist das hinnehmbar. Es ist trotzdem
// ein Unterschied im Verhalten und gehört dokumentiert.

#include "flutter_libnx/posix_compat.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>
#include <string>

namespace {

// Echte Deskriptoren vergibt newlib fortlaufend ab 0 und bleibt weit
// unterhalb dieser Grenze. Ein eigener Bereich kann daher nie mit ihnen
// kollidieren.
constexpr int kDirHandleBase = 0x40000000;

struct DirEntry {
  bool in_use = false;
  std::string path;
};

struct State {
  std::mutex mutex;
  DirEntry entries[flutter_libnx::kMaxOpenDirectories];
  size_t open_count = 0;
  size_t peak_count = 0;
  size_t exhausted = 0;
};

// Funktionslokal statt global: Die Wrapper können vor den Konstruktoren
// globaler Objekte laufen, wenn libc beim Start eine Datei schließt.
State& GetState() {
  static State state;
  return state;
}

bool IsDirHandle(int fd) {
  return fd >= kDirHandleBase &&
         fd < kDirHandleBase + static_cast<int>(flutter_libnx::kMaxOpenDirectories);
}

// Nur mit gehaltener Sperre aufrufen.
DirEntry* EntryFor(State& state, int fd) {
  if (!IsDirHandle(fd)) {
    return nullptr;
  }
  DirEntry& entry = state.entries[fd - kDirHandleBase];
  return entry.in_use ? &entry : nullptr;
}

int AllocHandle(const std::string& path) {
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  for (size_t i = 0; i < flutter_libnx::kMaxOpenDirectories; ++i) {
    if (!state.entries[i].in_use) {
      state.entries[i].in_use = true;
      state.entries[i].path = path;
      state.open_count++;
      if (state.open_count > state.peak_count) {
        state.peak_count = state.open_count;
      }
      return kDirHandleBase + static_cast<int>(i);
    }
  }
  state.exhausted++;
  return -1;
}

bool ReleaseHandle(int fd) {
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  DirEntry* entry = EntryFor(state, fd);
  if (entry == nullptr) {
    return false;
  }
  entry->in_use = false;
  entry->path.clear();
  state.open_count--;
  return true;
}

bool LookupPath(int fd, std::string* out) {
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  DirEntry* entry = EntryFor(state, fd);
  if (entry == nullptr) {
    return false;
  }
  *out = entry->path;
  return true;
}

// Horizon-Pfade tragen ein Gerätepräfix ("sdmc:/switch/..."). Als absolut
// gilt daher beides: der klassische Schrägstrich am Anfang und ein
// Gerätename vor dem ersten Trennzeichen.
bool IsAbsolutePath(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (path[0] == '/') {
    return true;
  }
  const char* colon = strchr(path, ':');
  const char* slash = strchr(path, '/');
  return colon != nullptr && (slash == nullptr || colon < slash);
}

std::string JoinPath(const std::string& base, const char* leaf) {
  if (base.empty()) {
    return leaf;
  }
  std::string result = base;
  const char last = result[result.size() - 1];
  if (last != '/' && last != ':') {
    result += '/';
  }
  result += leaf;
  return result;
}

// Setzt aus Basisdeskriptor und Pfad den vollständigen Pfad zusammen.
// Rückgabe false heißt: Der Deskriptor ist keiner, den wir kennen – dann
// gibt es keinen Weg, daraus einen Pfad zu machen.
bool ResolveAt(int dirfd, const char* path, std::string* out) {
  if (path == nullptr) {
    errno = EINVAL;
    return false;
  }
  if (IsAbsolutePath(path) || dirfd == AT_FDCWD) {
    // Relative Pfade löst libnx selbst gegen das Arbeitsverzeichnis auf.
    *out = path;
    return true;
  }
  std::string base;
  if (!LookupPath(dirfd, &base)) {
    // Ein echter Dateideskriptor als Basisverzeichnis. Das kann auf dieser
    // Plattform nicht vorkommen, weil Verzeichnisse nie echte Deskriptoren
    // bekommen – also ist der Aufrufer im Irrtum.
    errno = EBADF;
    return false;
  }
  *out = JoinPath(base, path);
  return true;
}

bool PathIsDirectory(const std::string& path) {
  struct stat st = {};
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

}  // namespace

extern "C" {

// Von --wrap bereitgestellt: die ursprünglichen libc-Fassungen.
int __real_close(int fd);
int __real_dup(int fd);
int __real_fstat(int fd, struct stat* st);

int openat(int dirfd, const char* path, int flags, ...) {
  std::string full;
  if (!ResolveAt(dirfd, path, &full)) {
    return -1;
  }

  // Ein Verzeichnis bekommt ein Handle, unabhängig davon, ob der Aufrufer
  // O_DIRECTORY gesetzt hat. fml öffnet Verzeichnisse an einer Stelle ganz
  // ohne dieses Flag (`IsDirectory(base, path)` geht über
  // `OpenFileReadOnly`), und dort muss das anschließende `fstat` trotzdem
  // S_ISDIR melden.
  if (PathIsDirectory(full)) {
    if ((flags & O_ACCMODE) != O_RDONLY) {
      errno = EISDIR;
      return -1;
    }
    const int handle = AllocHandle(full);
    if (handle < 0) {
      errno = EMFILE;
      return -1;
    }
    return handle;
  }

  if ((flags & O_DIRECTORY) != 0) {
    errno = ENOTDIR;
    return -1;
  }

  int mode = 0;
  if ((flags & O_CREAT) != 0) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }
  return ::open(full.c_str(), flags, mode);
}

int mkdirat(int dirfd, const char* path, mode_t mode) {
  std::string full;
  if (!ResolveAt(dirfd, path, &full)) {
    return -1;
  }
  return ::mkdir(full.c_str(), mode);
}

int unlinkat(int dirfd, const char* path, int flags) {
  std::string full;
  if (!ResolveAt(dirfd, path, &full)) {
    return -1;
  }
  if ((flags & AT_REMOVEDIR) != 0) {
    return ::rmdir(full.c_str());
  }
  return ::unlink(full.c_str());
}

int faccessat(int dirfd, const char* path, int mode, int flags) {
  std::string full;
  if (!ResolveAt(dirfd, path, &full)) {
    return -1;
  }
  // AT_EACCESS unterscheidet effektive von realen Benutzerrechten. Horizon
  // kennt keine Benutzer, beide sind dasselbe – das Flag ist bedeutungslos,
  // nicht unerfüllbar.
  (void)flags;
  return ::access(full.c_str(), mode);
}

int renameat(int old_dirfd, const char* old_path, int new_dirfd,
             const char* new_path) {
  std::string full_old;
  std::string full_new;
  if (!ResolveAt(old_dirfd, old_path, &full_old) ||
      !ResolveAt(new_dirfd, new_path, &full_new)) {
    return -1;
  }
  return ::rename(full_old.c_str(), full_new.c_str());
}

DIR* fdopendir(int fd) {
  std::string path;
  if (!LookupPath(fd, &path)) {
    errno = ENOTDIR;
    return nullptr;
  }
  DIR* dir = ::opendir(path.c_str());
  if (dir == nullptr) {
    return nullptr;
  }
  // POSIX schreibt vor, dass fdopendir den Deskriptor übernimmt und
  // closedir ihn schließt. Der zurückgegebene DIR* hängt hier an keinem
  // Deskriptor mehr, deshalb wird das Handle sofort frei. Das Ergebnis ist
  // dasselbe: Der Aufrufer darf fd nicht mehr benutzen, und closedir räumt
  // den Rest auf.
  ReleaseHandle(fd);
  return dir;
}

int dirfd(DIR* dirp) {
  // Nicht abbildbar: Ein DIR_ITER von libnx hat keinen Deskriptor, aus dem
  // sich einer machen ließe. Ein erfundener Wert wäre schlimmer als ein
  // Fehler, weil er erst später und an anderer Stelle auffiele.
  (void)dirp;
  errno = ENOTSUP;
  return -1;
}

int __wrap_close(int fd) {
  if (ReleaseHandle(fd)) {
    return 0;
  }
  if (IsDirHandle(fd)) {
    errno = EBADF;
    return -1;
  }
  return __real_close(fd);
}

int __wrap_dup(int fd) {
  std::string path;
  if (LookupPath(fd, &path)) {
    const int handle = AllocHandle(path);
    if (handle < 0) {
      errno = EMFILE;
      return -1;
    }
    return handle;
  }
  if (IsDirHandle(fd)) {
    errno = EBADF;
    return -1;
  }
  return __real_dup(fd);
}

int __wrap_fstat(int fd, struct stat* st) {
  std::string path;
  if (LookupPath(fd, &path)) {
    return ::stat(path.c_str(), st);
  }
  if (IsDirHandle(fd)) {
    errno = EBADF;
    return -1;
  }
  return __real_fstat(fd, st);
}

}  // extern "C"

namespace flutter_libnx {

PosixCompatStats GetPosixCompatStats() {
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  PosixCompatStats stats;
  stats.open_directories = state.open_count;
  stats.peak_directories = state.peak_count;
  stats.table_exhausted = state.exhausted;
  return stats;
}

}  // namespace flutter_libnx
