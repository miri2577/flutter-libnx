// Zentrales Logging für den flutter-libnx-Embedder.
//
// Bei einer Portierung wie dieser ist Logging kein Komfort, sondern das
// Hauptdiagnosewerkzeug: Auf der Switch gibt es keinen Debugger im üblichen Sinn.
// Deshalb drei Senken, einzeln zuschaltbar: Konsole, nxlink (Netzwerk), Datei auf SD.
//
// Regel: kein Fehler wird kommentarlos verschluckt.
#pragma once

#include <cstdarg>

namespace flutter_libnx {

enum class LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

struct LogConfig {
  // Ausgabe auf stdout. Nützlich zusammen mit consoleInit(), aber unvereinbar
  // mit einem Framebuffer auf demselben Fenster – dann nxlink oder Datei nutzen.
  bool to_stdout = false;

  // stdout/stderr über nxlink zum Host umleiten. Schlägt still fehl, wenn kein
  // nxlink-Host da ist; das ist kein Fehler, sondern der Normalfall auf Hardware.
  bool to_nxlink = true;

  // Absoluter Pfad auf der SD-Karte, z.B. "sdmc:/switch/flutter-libnx/app.log".
  // nullptr schaltet die Dateisenke ab.
  const char* file_path = nullptr;

  LogLevel min_level = LogLevel::kDebug;
};

// Gibt false zurück, wenn keine einzige Senke geöffnet werden konnte.
bool LogInit(const LogConfig& config);
void LogShutdown();

// true, wenn nxlink tatsächlich verbunden ist. Erst nach LogInit aussagekräftig.
bool LogHasNxlink();

void LogWrite(LogLevel level, const char* file, int line, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

}  // namespace flutter_libnx

#define LOG_DEBUG(...)                                                    \
  ::flutter_libnx::LogWrite(::flutter_libnx::LogLevel::kDebug, __FILE__, \
                            __LINE__, __VA_ARGS__)
#define LOG_INFO(...)                                                    \
  ::flutter_libnx::LogWrite(::flutter_libnx::LogLevel::kInfo, __FILE__, \
                            __LINE__, __VA_ARGS__)
#define LOG_WARN(...)                                                    \
  ::flutter_libnx::LogWrite(::flutter_libnx::LogLevel::kWarn, __FILE__, \
                            __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)                                                    \
  ::flutter_libnx::LogWrite(::flutter_libnx::LogLevel::kError, __FILE__, \
                            __LINE__, __VA_ARGS__)
