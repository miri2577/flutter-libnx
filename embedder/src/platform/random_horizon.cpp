// Zufallsquelle für Horizon.
//
// newlib bringt `getentropy()` mit, reicht aber an `_getentropy_r` weiter,
// das die Plattform zu stellen hat – und für Horizon stellt es niemand.
// Betroffen ist nicht nur Dart: BoringSSL zieht dieselbe Funktion.
//
// Die Switch hat dafür einen eigenen Dienst. `csrngGetRandomBytes` spricht
// csrng an, den kryptographisch geeigneten Generator des Systems; `randomGet`
// wäre der schnellere, aber nicht dafür gedachte Weg.
//
// Warum eine eigene Datei: `<switch.h>` bringt Makros mit, die sich mit
// gewöhnlichen C++-Bezeichnern beißen. Dieselbe Erfahrung hatte schon
// `os_horizon.cc` im Dart-Baum. Hier steht der Header allein, und alles
// andere bleibt davon unberührt.

#include <switch.h>

#include <errno.h>
#include <stddef.h>
#include <sys/reent.h>

extern "C" {

// POSIX begrenzt getentropy auf 256 Bytes je Aufruf. Die Grenze gilt hier
// genauso, damit sich Aufrufer plattformübergreifend gleich verhalten.
constexpr size_t kMaxEntropyPerCall = 256;

int _getentropy_r(struct _reent* reent, void* buffer, size_t length) {
  if (length > kMaxEntropyPerCall) {
    reent->_errno = EIO;
    return -1;
  }
  if (buffer == nullptr) {
    reent->_errno = EFAULT;
    return -1;
  }

  const Result result = csrngGetRandomBytes(buffer, length);
  if (R_FAILED(result)) {
    // Ohne belastbaren Zufall ist Schweigen die schlechteste Antwort: Der
    // Aufrufer würde mit uninitialisiertem Speicher weiterarbeiten und ihn
    // für zufällig halten.
    reent->_errno = EIO;
    return -1;
  }
  return 0;
}

}  // extern "C"
