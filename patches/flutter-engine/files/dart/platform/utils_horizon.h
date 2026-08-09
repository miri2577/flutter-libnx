// Copyright 2026 The flutter-libnx Authors.
//
// Utils-Spezialisierungen für Horizon OS (Nintendo Switch, devkitA64/newlib).
//
// Anders als glibc bringt newlib weder <endian.h> noch <byteswap.h> mit, und
// strerror_r ist nicht deklariert. Beides wird deshalb selbsttragend über
// Compiler-Builtins bzw. strerror gelöst, statt Header vorauszusetzen, die es
// hier nicht gibt.

#ifndef RUNTIME_PLATFORM_UTILS_HORIZON_H_
#define RUNTIME_PLATFORM_UTILS_HORIZON_H_

#if !defined(RUNTIME_PLATFORM_UTILS_H_)
#error Do not include utils_horizon.h directly; use utils.h instead.
#endif

#include <cstdio>
#include <cstring>

namespace dart {

// __builtin_bswap* kennt GCC seit jeher; __BYTE_ORDER__ ebenfalls. Damit
// entfällt jede Abhängigkeit von plattformspezifischen Headern.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

inline uint16_t Utils::HostToBigEndian16(uint16_t value) {
  return __builtin_bswap16(value);
}

inline uint32_t Utils::HostToBigEndian32(uint32_t value) {
  return __builtin_bswap32(value);
}

inline uint64_t Utils::HostToBigEndian64(uint64_t value) {
  return __builtin_bswap64(value);
}

inline uint16_t Utils::HostToLittleEndian16(uint16_t value) {
  return value;
}

inline uint32_t Utils::HostToLittleEndian32(uint32_t value) {
  return value;
}

inline uint64_t Utils::HostToLittleEndian64(uint64_t value) {
  return value;
}

#else  // Big Endian – auf der Switch nicht der Fall, der Vollständigkeit halber.

inline uint16_t Utils::HostToBigEndian16(uint16_t value) {
  return value;
}

inline uint32_t Utils::HostToBigEndian32(uint32_t value) {
  return value;
}

inline uint64_t Utils::HostToBigEndian64(uint64_t value) {
  return value;
}

inline uint16_t Utils::HostToLittleEndian16(uint16_t value) {
  return __builtin_bswap16(value);
}

inline uint32_t Utils::HostToLittleEndian32(uint32_t value) {
  return __builtin_bswap32(value);
}

inline uint64_t Utils::HostToLittleEndian64(uint64_t value) {
  return __builtin_bswap64(value);
}

#endif

inline char* Utils::StrError(int err, char* buffer, size_t bufsize) {
  // newlib deklariert kein strerror_r. strerror ist hier vertretbar: Für
  // bekannte Fehlercodes liefert newlib Zeiger auf konstante Zeichenketten,
  // und der Rückfallpuffer für unbekannte Codes liegt in der pro Thread
  // geführten Reentrancy-Struktur.
  snprintf(buffer, bufsize, "%s", strerror(err));
  return buffer;
}

}  // namespace dart

#endif  // RUNTIME_PLATFORM_UTILS_HORIZON_H_
