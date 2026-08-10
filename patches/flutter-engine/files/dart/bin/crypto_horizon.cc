// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/globals.h"
#if defined(DART_HOST_OS_HORIZON)

#include <errno.h>
#include <unistd.h>

#include "bin/crypto.h"

namespace dart {
namespace bin {

// crypto_linux.cc holt sich Zufall aus /dev/urandom, ersatzweise ueber
// SYS_getrandom. Beides gibt es auf Horizon nicht: Es fuehrt kein /dev, und
// Linux-Syscallnummern haben hier keine Bedeutung.
//
// getentropy() dagegen deklariert newlib, und die Umsetzung fuer diese
// Plattform steht in embedder/src/platform/random_horizon.cpp. Sie fragt
// csrng, den kryptographisch geeigneten Generator des Systems.
//
// POSIX begrenzt getentropy auf 256 Bytes je Aufruf, deshalb die Schleife.
bool Crypto::GetRandomBytes(intptr_t count, uint8_t* buffer) {
  constexpr intptr_t kMaxPerCall = 256;

  intptr_t remaining = count;
  uint8_t* cursor = buffer;
  while (remaining > 0) {
    const intptr_t chunk = remaining < kMaxPerCall ? remaining : kMaxPerCall;
    if (getentropy(cursor, static_cast<size_t>(chunk)) != 0) {
      return false;
    }
    cursor += chunk;
    remaining -= chunk;
  }
  return true;
}

}  // namespace bin
}  // namespace dart

#endif  // defined(DART_HOST_OS_HORIZON)
