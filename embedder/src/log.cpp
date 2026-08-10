#include "flutter_libnx/log.h"

#include <switch.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>  // close() für die Sockets

namespace flutter_libnx {
namespace {

// Bewusst dateilokaler Zustand statt Singleton-Klasse: Das Logging muss auch dann
// noch funktionieren, wenn sonst nichts mehr steht, und darf keine Konstruktions-
// reihenfolge voraussetzen.
LogConfig g_config;
FILE* g_file = nullptr;
int g_nxlink_fd = -1;
int g_remote_fd = -1;
bool g_socket_ready = false;
bool g_initialized = false;

// Der Socket-Dienst darf nur einmal hochgefahren werden, wird aber von zwei
// Senken gebraucht.
bool EnsureSocket() {
  if (g_socket_ready) {
    return true;
  }
  if (R_SUCCEEDED(socketInitializeDefault())) {
    g_socket_ready = true;
  }
  return g_socket_ready;
}

// Verbindet mit Zeitlimit. Ohne das würde ein fehlender Empfänger den Start der
// Anwendung blockieren – und genau beim Debuggen läuft der Empfänger oft nicht.
int ConnectWithTimeout(const char* host, uint16_t port, int timeout_ms) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    close(fd);
    return -1;
  }

  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc != 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }

  if (rc != 0) {
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(fd + 1, nullptr, &write_set, nullptr, &tv) <= 0) {
      close(fd);
      return -1;
    }

    int sock_err = 0;
    socklen_t len = sizeof(sock_err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &len) < 0 || sock_err != 0) {
      close(fd);
      return -1;
    }
  }

  fcntl(fd, F_SETFL, flags);
  return fd;
}

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo:  return "INFO ";
    case LogLevel::kWarn:  return "WARN ";
    case LogLevel::kError: return "ERROR";
  }
  return "?????";
}

// Legt alle Verzeichnisse eines Pfades an. mkdir() liefert EEXIST, das ist ok.
void MakeParentDirs(const char* path) {
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer), "%s", path);

  // Hinter dem devoptab-Präfix ("sdmc:/") beginnen, sonst versuchen wir "sdmc:" anzulegen.
  char* cursor = std::strchr(buffer, ':');
  cursor = cursor ? cursor + 1 : buffer;
  if (*cursor == '/') {
    ++cursor;
  }

  for (; *cursor != '\0'; ++cursor) {
    if (*cursor != '/') {
      continue;
    }
    *cursor = '\0';
    mkdir(buffer, 0777);
    *cursor = '/';
  }
}

const char* BaseName(const char* path) {
  const char* slash = std::strrchr(path, '/');
  if (slash != nullptr) {
    return slash + 1;
  }
  const char* backslash = std::strrchr(path, '\\');
  return backslash != nullptr ? backslash + 1 : path;
}

}  // namespace

bool LogInit(const LogConfig& config) {
  g_config = config;
  bool any_sink = false;

  if (config.to_nxlink) {
    // Darf fehlschlagen (kein Netzwerk, kein nxlink-Host) – die anderen Senken
    // müssen davon unberührt weiterlaufen.
    if (EnsureSocket()) {
      g_nxlink_fd = nxlinkStdio();
      if (g_nxlink_fd >= 0) {
        any_sink = true;
      }
    }
  }

  if (config.remote_host != nullptr) {
    if (EnsureSocket()) {
      g_remote_fd = ConnectWithTimeout(config.remote_host, config.remote_port, 2000);
      if (g_remote_fd >= 0) {
        any_sink = true;
      }
    }
  }

  if (config.to_stdout) {
    any_sink = true;
  }

  if (config.file_path != nullptr) {
    MakeParentDirs(config.file_path);
    g_file = std::fopen(config.file_path, "w");
    if (g_file != nullptr) {
      any_sink = true;
    }
  }

  g_initialized = true;

  if (any_sink) {
    LogWrite(LogLevel::kInfo, __FILE__, __LINE__,
             "Logging bereit (stdout=%d nxlink=%d netz=%d datei=%s)",
             config.to_stdout ? 1 : 0, g_nxlink_fd >= 0 ? 1 : 0,
             g_remote_fd >= 0 ? 1 : 0,
             g_file != nullptr ? config.file_path : "aus");
  }
  return any_sink;
}

void LogShutdown() {
  if (g_file != nullptr) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  if (g_remote_fd >= 0) {
    close(g_remote_fd);
    g_remote_fd = -1;
  }
  if (g_nxlink_fd >= 0) {
    close(g_nxlink_fd);
    g_nxlink_fd = -1;
  }
  if (g_socket_ready) {
    socketExit();
    g_socket_ready = false;
  }
  g_initialized = false;
}

bool LogHasNxlink() {
  return g_nxlink_fd >= 0;
}

bool LogHasRemote() {
  return g_remote_fd >= 0;
}

void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...) {
  if (!g_initialized || level < g_config.min_level) {
    return;
  }

  char message[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  char line_buffer[1200];
  std::snprintf(line_buffer, sizeof(line_buffer), "[flutter-libnx][%s] %s (%s:%d)\n",
                LevelName(level), message, BaseName(file), line);

  // stdout deckt Konsole und nxlink gemeinsam ab, weil nxlinkStdio() stdout umleitet.
  if (g_config.to_stdout || g_nxlink_fd >= 0) {
    std::fputs(line_buffer, stdout);
    std::fflush(stdout);
  }

  if (g_file != nullptr) {
    std::fputs(line_buffer, g_file);
    // Sofort schreiben: Bei einem Absturz ist genau die letzte Zeile die wichtige.
    std::fflush(g_file);
  }

  if (g_remote_fd >= 0) {
    const size_t len = std::strlen(line_buffer);
    size_t written = 0;
    while (written < len) {
      const ssize_t n = send(g_remote_fd, line_buffer + written, len - written, 0);
      if (n <= 0) {
        // Verbindung weg. Senke stilllegen statt bei jeder Zeile erneut zu
        // scheitern – die übrigen Senken laufen weiter.
        close(g_remote_fd);
        g_remote_fd = -1;
        break;
      }
      written += static_cast<size_t>(n);
    }
  }
}

}  // namespace flutter_libnx

// Gegenstelle zu der schwach gebundenen Funktion in os_horizon.cc.
//
// Die Dart-VM schreibt ihre Meldungen sonst nur nach stdout und stderr, und
// die sind auf der Konsole unsichtbar. Gerade die letzte Zeile vor einem
// abort() ist aber die aufschlussreichste. Über diesen Weg landet sie in
// derselben Senke wie alles andere.
extern "C" void flutter_libnx_vm_log(const char* text) {
  if (text == nullptr) {
    return;
  }
  // Die VM setzt ihre Zeilenumbrüche selbst; der Logger fügt einen eigenen an.
  // Ohne dieses Abschneiden entstünden Leerzeilen.
  std::string line(text);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }
  if (line.empty()) {
    return;
  }
  flutter_libnx::LogWrite(flutter_libnx::LogLevel::kInfo, "dart-vm", 0, "%s",
                          line.c_str());
}
