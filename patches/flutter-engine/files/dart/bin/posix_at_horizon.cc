// Copyright 2026 The flutter-libnx Authors.
//
// Die *at-Familie für Horizon OS.
//
// `openat`, `faccessat` und `renameat` nehmen ein Verzeichnis-Handle entgegen
// und lösen den Pfad relativ dazu auf. Sie gibt es in newlib nicht – `openat`
// ist zwar deklariert (sys/_default_fcntl.h:222), aber nicht implementiert.
//
// Dart benutzt sie für seine Namensraum-Trennung (`Namespace`), die auf
// Horizon gegenstandslos ist: Es gibt keine Verzeichnis-Deskriptoren und keine
// Mount-Namensräume, sondern devoptab-Geräte wie `sdmc:/`.
//
// Deshalb behandeln diese Fassungen ausschließlich AT_FDCWD – also die
// Auflösung gegen das Arbeitsverzeichnis, was Dart in dieser Konfiguration
// auch verlangt. Jeder andere Deskriptor wird mit ENOTSUP abgelehnt, statt
// stillschweigend etwas anderes zu tun als angefordert.

#include "platform/globals.h"
#if defined(DART_HOST_OS_HORIZON)

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

extern "C" {

int openat(int dirfd, const char* path, int flags, ...) {
  if (dirfd != AT_FDCWD) {
    errno = ENOTSUP;
    return -1;
  }
  // Der dritte Parameter ist nur bei O_CREAT vorhanden.
  mode_t mode = 0;
  if ((flags & O_CREAT) != 0) {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  return open(path, flags, mode);
}

int faccessat(int dirfd, const char* path, int mode, int flags) {
  if (dirfd != AT_FDCWD) {
    errno = ENOTSUP;
    return -1;
  }
  // flags kennt AT_EACCESS und AT_SYMLINK_NOFOLLOW; beides ist hier ohne
  // Bedeutung, weil es weder getrennte effektive Nutzerkennungen noch
  // symbolische Verknuepfungen gibt.
  (void)flags;
  return access(path, mode);
}

int renameat(int olddirfd, const char* oldpath, int newdirfd,
             const char* newpath) {
  if ((olddirfd != AT_FDCWD) || (newdirfd != AT_FDCWD)) {
    errno = ENOTSUP;
    return -1;
  }
  return rename(oldpath, newpath);
}

}  // extern "C"

#endif  // defined(DART_HOST_OS_HORIZON)
