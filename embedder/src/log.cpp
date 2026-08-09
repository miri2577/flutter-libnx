#include "flutter_libnx/log.h"

#include <switch.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>  // close() für den nxlink-Socket

namespace flutter_libnx {
namespace {

// Bewusst dateilokaler Zustand statt Singleton-Klasse: Das Logging muss auch dann
// noch funktionieren, wenn sonst nichts mehr steht, und darf keine Konstruktions-
// reihenfolge voraussetzen.
LogConfig g_config;
FILE* g_file = nullptr;
int g_nxlink_fd = -1;
bool g_initialized = false;

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
    // socketInitializeDefault() darf fehlschlagen (kein Netzwerk) – dann gibt es
    // eben kein nxlink, aber die anderen Senken müssen weiterlaufen.
    if (R_SUCCEEDED(socketInitializeDefault())) {
      g_nxlink_fd = nxlinkStdio();
      if (g_nxlink_fd >= 0) {
        any_sink = true;
      } else {
        socketExit();
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
             "Logging bereit (stdout=%d nxlink=%d datei=%s)",
             config.to_stdout ? 1 : 0, g_nxlink_fd >= 0 ? 1 : 0,
             g_file != nullptr ? config.file_path : "aus");
  }
  return any_sink;
}

void LogShutdown() {
  if (g_file != nullptr) {
    std::fclose(g_file);
    g_file = nullptr;
  }
  if (g_nxlink_fd >= 0) {
    close(g_nxlink_fd);
    g_nxlink_fd = -1;
    socketExit();
  }
  g_initialized = false;
}

bool LogHasNxlink() {
  return g_nxlink_fd >= 0;
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
}

}  // namespace flutter_libnx
