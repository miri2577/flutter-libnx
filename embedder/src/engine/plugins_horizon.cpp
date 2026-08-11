// Gruppe-B-Plugins: path_provider und shared_preferences.
//
// Warum diese zwei zuerst: Sie tauchen in nahezu jeder Flutter-App auf
// (docs/target-apps.md), und der erste Lauf einer echten fremden App
// (rezkonv_app) ist exakt an ihnen gescheitert - shared_preferences.getAll
// beim Start, path_provider.getTemporaryDirectory vor jedem
// Datenbankzugriff von drift.
//
// Beide Kanäle sprechen den StandardMethodCodec (Binärformat), nicht JSON -
// deshalb existiert flutter_libnx/standard_message_codec.h.
//
// Ablage: /switch/flutter_apps/<app-id>/ auf der SD-Karte. Der Pfad ist
// absichtlich ohne "sdmc:"-Präfix - hbmenu startet mit sdmc als
// Standardgerät, und ein POSIX-reiner Pfad übersteht die Pfadarithmetik
// des Dart-path-Pakets (p.join, p.normalize, isAbsolute) unbeschädigt.
//
// shared_preferences persistiert als codec-kodierte Map in einer Datei:
// dasselbe Format wie auf dem Draht, kein eigener Parser, atomar über
// Nebendatei + rename.
//
// Alle Handler laufen auf dem Plattform-Thread (der Hauptschleife) - keine
// Nebenläufigkeit, keine Sperren.

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "embedder.h"
#include "flutter_libnx/log.h"
#include "flutter_libnx/standard_message_codec.h"

namespace flutter_libnx {

namespace {

// Später aus der NACP; für den Beweis reicht eine feste Kennung.
constexpr const char* kAppId = "ui_app";

std::string BaseDir() {
  return std::string("/switch/flutter_apps/") + kAppId;
}

// mkdir -p. FAT kennt keine Rechte, der Modus ist Formsache.
bool EnsureDir(const std::string& path) {
  std::string partial;
  for (size_t i = 1; i <= path.size(); i++) {
    if (i == path.size() || path[i] == '/') {
      partial = path.substr(0, i);
      if (mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST) {
        LOG_ERROR("mkdir %s: errno %d", partial.c_str(), errno);
        return false;
      }
    }
  }
  return true;
}

void Respond(FLUTTER_API_SYMBOL(FlutterEngine) engine,
             const FlutterPlatformMessage* message,
             const std::vector<uint8_t>& payload) {
  if (message->response_handle == nullptr) {
    return;
  }
  FlutterEngineSendPlatformMessageResponse(engine, message->response_handle,
                                           payload.data(), payload.size());
}

// --- path_provider ----------------------------------------------------------

bool HandlePathProvider(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                        const FlutterPlatformMessage* message,
                        const std::string& method) {
  const char* subdir = nullptr;
  if (method == "getTemporaryDirectory") {
    subdir = "tmp";
  } else if (method == "getApplicationDocumentsDirectory") {
    subdir = "documents";
  } else if (method == "getApplicationSupportDirectory") {
    subdir = "support";
  } else if (method == "getLibraryDirectory") {
    subdir = "library";
  } else if (method == "getDownloadsDirectory") {
    subdir = "downloads";
  } else if (method == "getApplicationCachePath" ||
             method == "getApplicationCacheDirectory") {
    subdir = "cache";
  } else if (method == "getExternalStorageDirectory") {
    // Kein Zweitspeicher auf der Konsole; null ist die dokumentierte Antwort.
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  } else if (method == "getExternalCacheDirectories" ||
             method == "getExternalStorageDirectories") {
    Respond(engine, message, EncodeStdSuccess(StdValue::List()));
    return true;
  } else {
    LOG_WARN("path_provider: unbekannte Methode '%s'", method.c_str());
    return false;  // leere Antwort -> MissingPluginException, ehrlich
  }

  const std::string path = BaseDir() + "/" + subdir;
  if (!EnsureDir(path)) {
    Respond(engine, message,
            EncodeStdError("io", "Verzeichnis nicht anlegbar: " + path));
    return true;
  }
  Respond(engine, message, EncodeStdSuccess(StdValue::String(path)));
  return true;
}

// --- shared_preferences -----------------------------------------------------

StdValue g_prefs = StdValue::Map();
bool g_prefs_loaded = false;

std::string PrefsPath() {
  return BaseDir() + "/shared_preferences.std";
}

void LoadPrefs() {
  if (g_prefs_loaded) {
    return;
  }
  g_prefs_loaded = true;
  g_prefs = StdValue::Map();

  FILE* f = fopen(PrefsPath().c_str(), "rb");
  if (f == nullptr) {
    return;  // Erste Benutzung: leer ist der richtige Zustand.
  }
  std::vector<uint8_t> data;
  uint8_t chunk[4096];
  size_t n = 0;
  while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    data.insert(data.end(), chunk, chunk + n);
  }
  fclose(f);

  size_t cursor = 0;
  StdValue loaded;
  if (DecodeStdValue(data.data(), data.size(), &cursor, &loaded) &&
      loaded.type == StdValue::Type::kMap) {
    g_prefs = std::move(loaded);
  } else {
    LOG_ERROR("shared_preferences: Datei unlesbar, beginne leer");
  }
}

bool SavePrefs() {
  if (!EnsureDir(BaseDir())) {
    return false;
  }
  std::vector<uint8_t> data;
  EncodeStdValue(g_prefs, &data);

  const std::string path = PrefsPath();
  const std::string tmp = path + ".neu";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    LOG_ERROR("shared_preferences: %s nicht schreibbar (errno %d)",
              tmp.c_str(), errno);
    return false;
  }
  const bool written = fwrite(data.data(), 1, data.size(), f) == data.size();
  fclose(f);
  if (!written) {
    remove(tmp.c_str());
    return false;
  }
  // FAT hat kein atomares Ersetzen; Löschen + Umbenennen ist das Beste, was
  // die Karte hergibt. Schlimmster Fall bei Stromverlust: alte Datei weg,
  // neue liegt unter dem Nebennamen - der Lader beginnt dann leer.
  remove(path.c_str());
  if (rename(tmp.c_str(), path.c_str()) != 0) {
    LOG_ERROR("shared_preferences: rename nach %s: errno %d", path.c_str(),
              errno);
    return false;
  }
  return true;
}

const StdValue* MapGet(const StdValue& map, const std::string& key) {
  for (const auto& entry : map.as_map) {
    if (entry.first.type == StdValue::Type::kString &&
        entry.first.as_string == key) {
      return &entry.second;
    }
  }
  return nullptr;
}

void MapSet(StdValue* map, const std::string& key, StdValue value) {
  for (auto& entry : map->as_map) {
    if (entry.first.type == StdValue::Type::kString &&
        entry.first.as_string == key) {
      entry.second = std::move(value);
      return;
    }
  }
  map->as_map.emplace_back(StdValue::String(key), std::move(value));
}

bool HandleSharedPreferences(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                             const FlutterPlatformMessage* message,
                             const std::string& method,
                             const StdValue& args) {
  LoadPrefs();

  if (method == "getAll") {
    Respond(engine, message, EncodeStdSuccess(g_prefs));
    return true;
  }

  if (method == "setBool" || method == "setInt" || method == "setDouble" ||
      method == "setString" || method == "setStringList") {
    const StdValue* key = MapGet(args, "key");
    const StdValue* value = MapGet(args, "value");
    if (key == nullptr || key->type != StdValue::Type::kString ||
        value == nullptr) {
      Respond(engine, message,
              EncodeStdError("argument", "key/value fehlt bei " + method));
      return true;
    }
    MapSet(&g_prefs, key->as_string, *value);
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(SavePrefs())));
    return true;
  }

  if (method == "remove") {
    const StdValue* key = MapGet(args, "key");
    if (key != nullptr && key->type == StdValue::Type::kString) {
      auto& entries = g_prefs.as_map;
      for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].first.type == StdValue::Type::kString &&
            entries[i].first.as_string == key->as_string) {
          entries.erase(entries.begin() + i);
          break;
        }
      }
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(SavePrefs())));
    return true;
  }

  if (method == "clear") {
    // Wie die echten Implementierungen: nur die "flutter."-Einträge, fremde
    // bleiben stehen.
    auto& entries = g_prefs.as_map;
    for (size_t i = entries.size(); i > 0; i--) {
      const StdValue& k = entries[i - 1].first;
      if (k.type == StdValue::Type::kString &&
          k.as_string.rfind("flutter.", 0) == 0) {
        entries.erase(entries.begin() + (i - 1));
      }
    }
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(SavePrefs())));
    return true;
  }

  if (method == "commit") {
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(true)));
    return true;
  }

  LOG_WARN("shared_preferences: unbekannte Methode '%s'", method.c_str());
  return false;
}

// --- file_picker ------------------------------------------------------------
//
// Einen Datei-Browser als eigene Vollbild-UI gibt es (noch) nicht. Die
// Switch-Konvention lautet stattdessen: Dateien, die eine App importieren
// soll, liegen in /switch/flutter_apps/<app-id>/import/ auf der SD-Karte.
// Ein "Datei wählen" liefert den Inhalt dieses Ordners - bei
// Einzelauswahl die neueste Datei, bei Mehrfachauswahl alle passenden.
// Ist der Ordner leer, kommt null zurück, was file_picker als "abgebrochen"
// versteht - die App zeigt dann ihren normalen Kein-Ergebnis-Weg.

std::string ImportDir() {
  return BaseDir() + "/import";
}

bool HasAllowedExtension(const std::string& name,
                         const std::vector<std::string>& extensions) {
  if (extensions.empty()) {
    return true;
  }
  const size_t dot = name.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = name.substr(dot + 1);
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  for (const std::string& allowed : extensions) {
    if (ext == allowed) {
      return true;
    }
  }
  return false;
}

bool HandleFilePicker(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                      const FlutterPlatformMessage* message,
                      const std::string& method,
                      const StdValue& args) {
  if (method == "clear") {
    Respond(engine, message, EncodeStdSuccess(StdValue::Bool(true)));
    return true;
  }
  EnsureDir(ImportDir());
  if (method == "dir") {
    Respond(engine, message, EncodeStdSuccess(StdValue::String(ImportDir())));
    return true;
  }

  // any / custom / media / image / video / audio - alle laufen auf eine
  // Dateiliste hinaus.
  std::vector<std::string> extensions;
  if (const StdValue* list = MapGet(args, "allowedExtensions")) {
    for (const StdValue& item : list->as_list) {
      if (item.type == StdValue::Type::kString) {
        std::string ext = item.as_string;
        for (char& c : ext) {
          if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
          }
        }
        extensions.push_back(ext);
      }
    }
  }
  const StdValue* multi = MapGet(args, "allowMultipleSelection");
  const bool allow_multiple =
      multi != nullptr && multi->type == StdValue::Type::kBool &&
      multi->as_bool;

  struct Candidate {
    std::string name;
    int64_t size;
    int64_t mtime;
  };
  std::vector<Candidate> found;

  DIR* dir = opendir(ImportDir().c_str());
  if (dir != nullptr) {
    while (dirent* entry = readdir(dir)) {
      const std::string name = entry->d_name;
      if (name == "." || name == "..") {
        continue;
      }
      if (!HasAllowedExtension(name, extensions)) {
        continue;
      }
      struct stat st = {};
      const std::string full = ImportDir() + "/" + name;
      if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        continue;
      }
      found.push_back({name, static_cast<int64_t>(st.st_size),
                       static_cast<int64_t>(st.st_mtime)});
    }
    closedir(dir);
  }

  if (found.empty()) {
    LOG_INFO("file_picker: keine passenden Dateien in %s - dorthin kopieren",
             ImportDir().c_str());
    Respond(engine, message, EncodeStdSuccess(StdValue::Null()));
    return true;
  }

  if (!allow_multiple) {
    // Neueste Datei gewinnt - wer gerade etwas auf die Karte kopiert hat,
    // meint genau diese.
    size_t best = 0;
    for (size_t i = 1; i < found.size(); i++) {
      if (found[i].mtime > found[best].mtime) {
        best = i;
      }
    }
    found = {found[best]};
  }

  StdValue result = StdValue::List();
  for (const Candidate& file : found) {
    StdValue entry = StdValue::Map();
    MapSet(&entry, "path", StdValue::String(ImportDir() + "/" + file.name));
    MapSet(&entry, "name", StdValue::String(file.name));
    MapSet(&entry, "size", StdValue::Int(file.size));
    MapSet(&entry, "bytes", StdValue::Null());
    MapSet(&entry, "identifier", StdValue::Null());
    result.as_list.push_back(std::move(entry));
    LOG_INFO("file_picker: liefere %s (%lld Bytes)", file.name.c_str(),
             static_cast<long long>(file.size));
  }
  Respond(engine, message, EncodeStdSuccess(result));
  return true;
}

}  // namespace

// Behandelt die Plugin-Kanäle. true heißt: beantwortet (oder bewusst der
// leeren Antwort des Aufrufers überlassen -> false zurückgeben!).
extern "C" bool flutter_libnx_handle_plugin_message(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message) {
  const bool is_path_provider =
      strcmp(message->channel, "plugins.flutter.io/path_provider") == 0;
  const bool is_shared_preferences =
      strcmp(message->channel, "plugins.flutter.io/shared_preferences") == 0;
  const bool is_file_picker =
      strcmp(message->channel, "miguelruivo.flutter.plugins.filepicker") == 0;
  if (!is_path_provider && !is_shared_preferences && !is_file_picker) {
    return false;
  }

  std::string method;
  StdValue args;
  if (!DecodeStdMethodCall(message->message, message->message_size, &method,
                           &args)) {
    LOG_WARN("%s: Nachricht nicht dekodierbar (%zu Bytes)", message->channel,
             message->message_size);
    return false;
  }

  if (is_path_provider) {
    return HandlePathProvider(engine, message, method);
  }
  if (is_file_picker) {
    return HandleFilePicker(engine, message, method, args);
  }
  return HandleSharedPreferences(engine, message, method, args);
}

}  // namespace flutter_libnx
