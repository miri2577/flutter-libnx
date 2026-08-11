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

namespace {

// csrng braucht eine Dienst-Session. Ohne csrngInitialize antwortet
// csrngGetRandomBytes mit LibnxError_NotInitialized - Random.secure() in
// Dart waere damit tot gewesen; der Pfad war bisher schlicht unbenutzt.
//
// Und weil der Wirtsprozess nichts vergisst (siehe porting-notes zu
// 2011-0102): Jedes Initialize bekommt sein Exit, hier ueber
// flutter_libnx_random_cleanup am Programmende.
Mutex g_csrng_lock = {};
bool g_csrng_ready = false;

bool EnsureCsrng() {
  mutexLock(&g_csrng_lock);
  if (!g_csrng_ready && R_SUCCEEDED(csrngInitialize())) {
    g_csrng_ready = true;
  }
  const bool ready = g_csrng_ready;
  mutexUnlock(&g_csrng_lock);
  return ready;
}

}  // namespace

extern "C" {

// POSIX begrenzt getentropy auf 256 Bytes je Aufruf. Die Grenze gilt hier
// genauso, damit sich Aufrufer plattformübergreifend gleich verhalten.
constexpr size_t kMaxEntropyPerCall = 256;

void flutter_libnx_random_cleanup(void) {
  mutexLock(&g_csrng_lock);
  if (g_csrng_ready) {
    csrngExit();
    g_csrng_ready = false;
  }
  mutexUnlock(&g_csrng_lock);
}

int _getentropy_r(struct _reent* reent, void* buffer, size_t length) {
  if (length > kMaxEntropyPerCall) {
    reent->_errno = EIO;
    return -1;
  }
  if (buffer == nullptr) {
    reent->_errno = EFAULT;
    return -1;
  }
  if (!EnsureCsrng()) {
    reent->_errno = EIO;
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
