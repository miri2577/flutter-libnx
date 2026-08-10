// POSIX-Ergänzungen für Horizon.
//
// Sie füllen eine Lücke der Plattform, kein Verhalten der Engine. Deshalb
// liegen sie hier und nicht als Patch im Engine-Baum.

#ifndef FLUTTER_LIBNX_POSIX_COMPAT_H_
#define FLUTTER_LIBNX_POSIX_COMPAT_H_

#include <cstddef>

namespace flutter_libnx {

// Zählwerte statt Logausgaben: diese Schicht liegt unter der Logsenke, die
// ihrerseits Dateien und Sockets benutzt. Ein Logaufruf aus `close()` heraus
// könnte sich selbst ins Bein schießen, ein Zähler nicht.
struct PosixCompatStats {
  // Wie viele Verzeichnis-Handles gerade vergeben sind.
  size_t open_directories;
  // Höchststand seit Programmstart – zeigt, ob die Tabelle knapp wird.
  size_t peak_directories;
  // Wie oft die Tabelle voll war. Jeder Treffer ist ein verlorenes
  // Verzeichnis und damit ein echter Fehler.
  size_t table_exhausted;
};

PosixCompatStats GetPosixCompatStats();

// Wie viele Verzeichnisse gleichzeitig offen sein dürfen.
constexpr size_t kMaxOpenDirectories = 64;

}  // namespace flutter_libnx

#endif  // FLUTTER_LIBNX_POSIX_COMPAT_H_
