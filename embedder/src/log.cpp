#include "flutter_libnx/log.h"

#include <switch.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
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
bool g_socket_fell_back = false;
bool g_initialized = false;

// Der Socket-Dienst darf nur einmal hochgefahren werden, wird aber von zwei
// Senken gebraucht.
bool EnsureSocket() {
  if (g_socket_ready) {
    return true;
  }
  // Nicht die Default-Konfiguration: Die ist fuer bescheidene Homebrew
  // ausgelegt (kleine Puffer, 3 BSD-Sessions) und brach bei der Referenz-App mit
  // ENOBUFS (errno 105) auf allen Verbindungen zusammen - die App faehrt
  // Dutzende parallele TLS-Verbindungen (DoH, WebDAV, Relay, Thumbnails).
  // Speicher ist reichlich da (3-GB-Heap); grosszuegige Puffer und
  // Sessions sind der richtige Tausch.
  // GELERNT (2026-08-16, zweiter Anlauf): 24/24 wurde abgelehnt (der stille
  // Rueckfall verschleierte das anfangs - er meldet sich jetzt). Verdacht:
  // Das Session-Limit des Dienstes, nicht der Speicher. Deshalb NUR die
  // Effizienz (Puffer-Pool) angehoben, Sessions bleiben bei 16. Der Bedarf
  // ist real: Mit Pool 8 war nach dem ersten Cloud-Film Schluss - der
  // Stream-Proxy verdoppelt die Sockets (mpv->Proxy->Cloud), und FFmpeg
  // bekam nicht mal mehr eine Loopback-Verbindung (ENOBUFS).
  constexpr SocketInitConfig kConfig = {
      .tcp_tx_buf_size = 128 * 1024,
      .tcp_rx_buf_size = 128 * 1024,
      .tcp_tx_buf_max_size = 512 * 1024,
      .tcp_rx_buf_max_size = 512 * 1024,
      .udp_tx_buf_size = 0x2400,
      .udp_rx_buf_size = 0xA500,
      .sb_efficiency = 24,
      .num_bsd_sessions = 16,
      .bsd_service_type = BsdServiceType_User,
  };
  const Result big = socketInitialize(&kConfig);
  if (R_SUCCEEDED(big)) {
    g_socket_ready = true;
  } else if (R_SUCCEEDED(socketInitializeDefault())) {
    // Rueckfall, falls die grosse Konfiguration abgelehnt wird. Die
    // Meldung kann erst NACH LogInit sichtbar werden - deshalb merkt sich
    // der Aufrufer das Ergebnis ueber flutter_libnx_socket_config_fell_back.
    g_socket_ready = true;
    g_socket_fell_back = true;
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
    // Rotation statt Ueberschreiben: Nach einem harten Konsolen-Absturz ist
    // die Datei die einzige Quelle des Absturzberichts (die TCP-Verbindung
    // friert ohne FIN ein und liefert die letzten Zeilen nie aus). Ein
    // Neustart mit "w" hat diese Beweise am 2026-08-16 einmal vernichtet.
    {
      const std::string previous = std::string(config.file_path) + ".alt";
      std::remove(previous.c_str());
      std::rename(config.file_path, previous.c_str());
    }
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
    if (g_socket_fell_back) {
      // Sichtbar machen, was vorher still passierte: Mit den Winz-Defaults
      // (3 Sessions) erklaert sich jedes spaetere ENOBUFS von selbst.
      LogWrite(LogLevel::kWarn, __FILE__, __LINE__,
               "Socket-Dienst laeuft mit DEFAULT-Konfiguration - die eigene "
               "wurde abgelehnt, ENOBUFS ist damit vorprogrammiert");
    }
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
