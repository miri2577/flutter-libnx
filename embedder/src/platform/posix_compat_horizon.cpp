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
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>
#include <string>

#include "flutter_libnx/log.h"

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

// --- Virtuelle Dateihandles -------------------------------------------------
//
// Fund vom 2026-08-15 (Referenz-App/Hive): Der Dateidienst haelt offene Dateien
// EXKLUSIV - ein zweites open() auf denselben Pfad scheitert mit EIO.
// Hive oeffnet jede Box doppelt (ein Lese-, ein Schreib-Handle mit eigener
// Position), sqlite oeffnet Journal neben Datenbank, und jede weitere
// Bibliothek mit diesem Muster liefe in dieselbe Wand.
//
// Deshalb vergibt open() hier grundsaetzlich VIRTUELLE Handles: Pro Pfad
// existiert genau ein echter Deskriptor (refcounted), jedes virtuelle
// Handle traegt seinen eigenen Offset, und die Wrapper (read/write/lseek/
// fstat/ftruncate/close) setzen vor jedem Zugriff die echte Position um.
// Eine Sperre serialisiert die Zugriffe auf den Basisdeskriptor.
//
// Sockets und Pipes sind nicht betroffen - sie entstehen nie ueber open().

constexpr int kFileHandleBase = 0x50000000;
constexpr int kMaxBaseFiles = 64;
constexpr int kMaxVirtualFiles = 128;

struct BaseFile {
  bool in_use = false;
  std::string path;
  int real_fd = -1;
  int refs = 0;
};

struct VirtualFile {
  bool in_use = false;
  int base = -1;
  off_t offset = 0;
  bool append = false;
};

struct FileState {
  std::mutex mutex;
  BaseFile bases[kMaxBaseFiles];
  VirtualFile files[kMaxVirtualFiles];
};

FileState& GetFileState() {
  static FileState state;
  return state;
}

bool IsFileHandle(int fd) {
  return fd >= kFileHandleBase && fd < kFileHandleBase + kMaxVirtualFiles;
}

// Nur mit gehaltener Sperre aufrufen.
VirtualFile* FileFor(FileState& state, int fd) {
  if (!IsFileHandle(fd)) {
    return nullptr;
  }
  VirtualFile& file = state.files[fd - kFileHandleBase];
  return file.in_use ? &file : nullptr;
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

long sysconf(int name) {
  // Gefragt wird in der Praxis nur nach zweierlei. abseils LowLevelAlloc
  // holt sich hierüber die Seitengröße für seine Arena.
  switch (name) {
    case _SC_PAGESIZE:
      // Horizon arbeitet mit 4-KB-Seiten. Dieselbe Größe verlangt libnx für
      // Thread-Stacks, siehe os_thread_horizon.cc.
      return 4096;
    case _SC_NPROCESSORS_ONLN:
      // Der Switch hat vier Cortex-A57-Kerne, von denen Horizon drei für
      // Anwendungen freigibt; der vierte gehört dem System. Derselbe Wert
      // steht in os_horizon.cc.
      return 3;
    default:
      // Nicht raten: Ein erfundener Wert wäre schlimmer als eine Absage,
      // weil er erst viel später und an anderer Stelle auffiele.
      errno = EINVAL;
      return -1;
  }
}

int fstatat(int dirfd, const char* path, struct stat* st, int flags) {
  std::string full;
  if (!ResolveAt(dirfd, path, &full)) {
    return -1;
  }
  // AT_SYMLINK_NOFOLLOW ist bedeutungslos: Das Dateisystem der Switch kennt
  // keine symbolischen Verweise, also gibt es nichts, dem man nicht folgen
  // könnte.
  (void)flags;
  return ::stat(full.c_str(), st);
}

int fchdir(int fd) {
  std::string path;
  if (!LookupPath(fd, &path)) {
    errno = EBADF;
    return -1;
  }
  return ::chdir(path.c_str());
}

// off_t ist hier bereits 64 Bit breit, weshalb die LFS-Varianten nichts
// anderes tun als ihre Grundformen.
int open64(const char* path, int flags, ...) {
  int mode = 0;
  if ((flags & O_CREAT) != 0) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }
  return openat(AT_FDCWD, path, flags, mode);
}

int openat64(int dirfd, const char* path, int flags, ...) {
  int mode = 0;
  if ((flags & O_CREAT) != 0) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }
  return openat(dirfd, path, flags, mode);
}

ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
  // Ohne echtes pread bleibt nur suchen, lesen, zurücksetzen. Das ist nicht
  // atomar, deshalb die Sperre: Zwei Threads am selben Deskriptor würden
  // sich sonst gegenseitig die Position verstellen. Sie gilt prozessweit
  // statt pro Deskriptor – grob, aber pread ist hier kein heißer Pfad.
  static std::mutex seek_mutex;
  std::lock_guard<std::mutex> lock(seek_mutex);

  const off_t previous = ::lseek(fd, 0, SEEK_CUR);
  if (previous < 0) {
    return -1;
  }
  if (::lseek(fd, offset, SEEK_SET) < 0) {
    return -1;
  }
  const ssize_t read_bytes = ::read(fd, buf, count);
  const int saved_errno = errno;
  ::lseek(fd, previous, SEEK_SET);
  errno = saved_errno;
  return read_bytes;
}

int pipe(int fds[2]) {
  // Horizon kennt keine Pipes – und auch kein Socketpaar als Ersatz: libnx
  // definiert `socketpair` nur pro forma und meldet ENOSYS
  // (nx/source/runtime/devices/socket.c:812, „Unimplementable"). Der Aufruf
  // bleibt trotzdem stehen, weil er genau diesen Fehler weiterreicht; ein
  // eigenes ENOSYS wäre dieselbe Antwort mit mehr Zeilen.
  //
  // Wer einen Weckkanal braucht, findet den Weg in eventhandler_horizon.cc:
  // ein selbst geknüpftes TCP-Paar über 127.0.0.1.
  return ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* oldset) {
  // Horizon stellt keine Signale zu. "Alles blockiert" und "nichts
  // blockiert" sind hier dasselbe, deshalb ist Nichtstun die richtige
  // Antwort und kein verschwiegener Fehler.
  (void)how;
  (void)set;
  if (oldset != nullptr) {
    sigemptyset(oldset);
  }
  return 0;
}

ssize_t readlinkat(int dirfd, const char* path, char* buf, size_t bufsize) {
  // Das Dateisystem der Switch kennt keine symbolischen Verweise. EINVAL ist
  // genau das, was POSIX für "ist kein Verweis" vorsieht – der Aufrufer
  // erfährt die Wahrheit also in seiner eigenen Sprache.
  (void)dirfd;
  (void)path;
  (void)buf;
  (void)bufsize;
  errno = EINVAL;
  return -1;
}

int symlinkat(const char* target, int dirfd, const char* path) {
  (void)target;
  (void)dirfd;
  (void)path;
  errno = ENOSYS;
  return -1;
}

int utimensat(int dirfd, const char* path, const struct timespec times[2],
              int flags) {
  // Zeitstempel nachträglich zu setzen bietet der Dateidienst nicht an.
  (void)dirfd;
  (void)path;
  (void)times;
  (void)flags;
  errno = ENOSYS;
  return -1;
}

// --- Wraps der virtuellen Dateihandles --------------------------------------

extern "C" int __real_open(const char* path, int flags, ...);
extern "C" ssize_t __real_write(int fd, const void* buf, size_t count);
extern "C" off_t __real_lseek(int fd, off_t offset, int whence);
extern "C" int __real_ftruncate(int fd, off_t length);
extern "C" int __real_fcntl(int fd, int cmd, ...);
extern "C" int __real_fsync(int fd);

extern "C" int __wrap_open(const char* path, int flags, ...) {
  mode_t mode = 0;
  if ((flags & O_CREAT) != 0) {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  if (path == nullptr) {
    errno = EINVAL;
    return -1;
  }

  FileState& state = GetFileState();
  std::lock_guard<std::mutex> lock(state.mutex);

  // Existiert schon ein Basisdeskriptor fuer den Pfad?
  int base_index = -1;
  for (int i = 0; i < kMaxBaseFiles; i++) {
    if (state.bases[i].in_use && state.bases[i].path == path) {
      base_index = i;
      break;
    }
  }

  if (base_index >= 0) {
    if ((flags & O_CREAT) != 0 && (flags & O_EXCL) != 0) {
      errno = EEXIST;
      return -1;
    }
    if ((flags & O_TRUNC) != 0) {
      __real_ftruncate(state.bases[base_index].real_fd, 0);
    }
  } else {
    // O_APPEND wird je virtuellem Handle emuliert. Der Basisdeskriptor
    // bekommt maximalen Zugriff (O_RDWR): Der erste Oeffner koennte nur
    // lesen wollen, ein spaeterer will schreiben - beide teilen sich
    // dasselbe echte Handle. Rueckfall auf die angefragten Rechte fuer
    // nur-lesbare Dateisysteme (romfs).
    int real = __real_open(
        path, (flags & ~(O_APPEND | O_ACCMODE)) | O_RDWR, mode);
    if (real < 0) {
      real = __real_open(path, flags & ~O_APPEND, mode);
    }
    if (real < 0) {
      return -1;
    }
    for (int i = 0; i < kMaxBaseFiles; i++) {
      if (!state.bases[i].in_use) {
        base_index = i;
        break;
      }
    }
    if (base_index < 0) {
      __real_close(real);
      errno = EMFILE;
      return -1;
    }
    state.bases[base_index].in_use = true;
    state.bases[base_index].path = path;
    state.bases[base_index].real_fd = real;
    state.bases[base_index].refs = 0;
  }

  for (int i = 0; i < kMaxVirtualFiles; i++) {
    if (!state.files[i].in_use) {
      state.files[i].in_use = true;
      state.files[i].base = base_index;
      state.files[i].offset = 0;
      state.files[i].append = (flags & O_APPEND) != 0;
      state.bases[base_index].refs++;
      return kFileHandleBase + i;
    }
  }
  if (state.bases[base_index].refs == 0) {
    __real_close(state.bases[base_index].real_fd);
    state.bases[base_index].in_use = false;
  }
  errno = EMFILE;
  return -1;
}

extern "C" ssize_t __wrap_write(int fd, const void* buf, size_t count) {
  FileState& state = GetFileState();
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      BaseFile& base = state.bases[file->base];
      if (file->append) {
        file->offset = __real_lseek(base.real_fd, 0, SEEK_END);
      } else if (__real_lseek(base.real_fd, file->offset, SEEK_SET) < 0) {
        return -1;
      }
      const ssize_t written = __real_write(base.real_fd, buf, count);
      if (written > 0) {
        file->offset += written;
      }
      return written;
    }
  }
  return __real_write(fd, buf, count);
}

extern "C" off_t __wrap_lseek(int fd, off_t offset, int whence) {
  FileState& state = GetFileState();
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      BaseFile& base = state.bases[file->base];
      off_t target = 0;
      if (whence == SEEK_SET) {
        target = offset;
      } else if (whence == SEEK_CUR) {
        target = file->offset + offset;
      } else if (whence == SEEK_END) {
        struct stat st = {};
        if (__real_fstat(base.real_fd, &st) != 0) {
          return -1;
        }
        target = st.st_size + offset;
      } else {
        errno = EINVAL;
        return -1;
      }
      if (target < 0) {
        errno = EINVAL;
        return -1;
      }
      file->offset = target;
      return target;
    }
  }
  return __real_lseek(fd, offset, whence);
}

extern "C" int __wrap_ftruncate(int fd, off_t length) {
  FileState& state = GetFileState();
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      return __real_ftruncate(state.bases[file->base].real_fd, length);
    }
  }
  return __real_ftruncate(fd, length);
}

// fsync auf einem virtuellen Handle: newlib kennt die Nummer nicht -> EBADF.
// Genau daran scheiterte RandomAccessFile.flush (Dart), das media_kit beim
// player.open ueber safe_local_storage aufruft; die Exception brach das
// Intro ab, bevor ueberhaupt Video lief. Uebersetzt auf den echten
// Deskriptor - und toleriert, dass devoptab/FAT kein fsync kann (EBADF/
// ENOSYS/EINVAL): Die Daten liegen nach write() beim Dateidienst,
// Haltbarkeit im Linux-Sinn gibt es auf der SD-Karte ohnehin nicht.
extern "C" int __wrap_fsync(int fd) {
  FileState& state = GetFileState();
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      const int rc = __real_fsync(state.bases[file->base].real_fd);
      if (rc != 0 &&
          (errno == EBADF || errno == ENOSYS || errno == EINVAL)) {
        return 0;
      }
      return rc;
    }
  }
  return __real_fsync(fd);
}

// Dateisperren: Es gibt genau einen Prozess auf der Konsole - jede Sperre
// ist trivially erfuellt. Dart (RandomAccessFile.lock, von Hive benutzt)
// erwartet Erfolg, nicht ENOSYS.
extern "C" int __wrap_fcntl(int fd, int cmd, ...) {
  if (cmd == F_SETLK || cmd == F_SETLKW || cmd == F_GETLK) {
    if (cmd == F_GETLK) {
      va_list args;
      va_start(args, cmd);
      struct flock* lock_info = va_arg(args, struct flock*);
      va_end(args);
      if (lock_info != nullptr) {
        lock_info->l_type = F_UNLCK;
      }
    }
    return 0;
  }
  va_list args;
  va_start(args, cmd);
  void* arg = va_arg(args, void*);
  va_end(args);
  return __real_fcntl(fd, cmd, arg);
}

// Für sqlite3 (os_unix.c, robust_open/robustFchown). FAT kennt keine
// Eigentümer: geteuid liefert bewusst NICHT 0 - als "root" würde sqlite
// versuchen, per fchown Eigentümer zu erhalten, die es hier nicht gibt.
uid_t geteuid(void) {
  return 1;
}

int fchown(int fd, uid_t owner, gid_t group) {
  // Nichts zu besitzen, nichts zu ändern - Erfolg ist die ehrliche Antwort.
  (void)fd;
  (void)owner;
  (void)group;
  return 0;
}

// devoptab-Falle (Fund 2026-08-11, sqlite3-Rauchprobe): stat() auf eine
// Datei, die gerade GEOEFFNET ist, scheitert mit EIO. sqlites getFileMode
// fragt beim Anlegen des Journals genau so die Rechte der offenen Datenbank
// ab und meldete den Fehlschlag als SQLITE_IOERR_FSTAT (1802). Die
// Gegenprobe ueber open+fstat liefert ehrliche Werte - ein zweites
// Lese-Handle auf eine offene Datei erlaubt der Dateidienst.
extern "C" int __real_stat(const char* path, struct stat* st);

extern "C" int __wrap_stat(const char* path, struct stat* st) {
  const int rc = __real_stat(path, st);
  if (rc == 0) {
    return 0;
  }
  const int saved_errno = errno;
  const int fd = ::open(path, O_RDONLY);
  if (fd >= 0) {
    const int frc = __real_fstat(fd, st);
    ::close(fd);
    if (frc == 0) {
      return 0;
    }
  }
  if (saved_errno == EIO && errno == EIO) {
    // Beides EIO heisst: Die Datei existiert, ist aber vom Dateidienst
    // exklusiv gehalten (das eigene offene Handle) - waere sie weg, kaeme
    // ENOENT (auf Hardware gemessen, siehe porting-notes). Die ehrliche
    // Minimalantwort: regulaere Datei, Standardrechte, Groesse unbekannt.
    // sqlites getFileMode braucht nur den Modus; wer die Groesse einer
    // Datei will, die er selbst offen haelt, hat fstat auf dem Handle.
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0644;
    st->st_nlink = 1;
    return 0;
  }
  errno = saved_errno;
  return -1;
}

// devoptab-Falle (Fund 2026-08-11, sqlite3-Rauchprobe): Lesen an oder
// hinter dem Dateiende liefert -1 statt der POSIX-Antwort 0 (EOF).
// sqlite liest routinemaessig ueber das Ende (Journal-Header, Seite 1
// einer frischen Datenbank) und deutete den falschen Fehler als
// "database disk image is malformed". Der Wrap stellt die POSIX-Semantik
// fuer regulaere Dateien her; alles andere (Sockets, Fehler mitten in der
// Datei) reicht er unveraendert durch.
extern "C" ssize_t __real_read(int fd, void* buf, size_t count);

// Lesen ab/hinter EOF liefert auf devoptab -1 statt 0 - dieselbe Falle
// betrifft die direkten __real_read-Zugriffe der Virtualisierung.
static ssize_t ReadWithEofFix(int real_fd, void* buf, size_t count) {
  const ssize_t got = __real_read(real_fd, buf, count);
  if (got >= 0) {
    return got;
  }
  const int saved_errno = errno;
  struct stat st = {};
  if (__real_fstat(real_fd, &st) == 0 && S_ISREG(st.st_mode)) {
    const off_t pos = __real_lseek(real_fd, 0, SEEK_CUR);
    if (pos >= 0 && pos >= st.st_size) {
      return 0;  // EOF, wie POSIX es verlangt
    }
  }
  errno = saved_errno;
  return -1;
}

extern "C" ssize_t __wrap_read(int fd, void* buf, size_t count) {
  FileState& state = GetFileState();
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      BaseFile& base = state.bases[file->base];
      if (__real_lseek(base.real_fd, file->offset, SEEK_SET) < 0) {
        return -1;
      }
      const ssize_t got = ReadWithEofFix(base.real_fd, buf, count);
      if (got > 0) {
        file->offset += got;
      }
      return got;
    }
  }
  return ReadWithEofFix(fd, buf, count);
}

int __wrap_close(int fd) {
  {
    FileState& state = GetFileState();
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      BaseFile& base = state.bases[file->base];
      file->in_use = false;
      base.refs--;
      if (base.refs == 0) {
        const int rc = __real_close(base.real_fd);
        base.in_use = false;
        base.path.clear();
        return rc;
      }
      return 0;
    }
  }
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
  {
    FileState& state = GetFileState();
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      for (int i = 0; i < kMaxVirtualFiles; i++) {
        if (!state.files[i].in_use) {
          state.files[i].in_use = true;
          state.files[i].base = file->base;
          state.files[i].offset = file->offset;
          state.files[i].append = file->append;
          state.bases[file->base].refs++;
          return kFileHandleBase + i;
        }
      }
      errno = EMFILE;
      return -1;
    }
  }
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
  {
    FileState& state = GetFileState();
    std::lock_guard<std::mutex> lock(state.mutex);
    VirtualFile* file = FileFor(state, fd);
    if (file != nullptr) {
      return __real_fstat(state.bases[file->base].real_fd, st);
    }
  }
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
