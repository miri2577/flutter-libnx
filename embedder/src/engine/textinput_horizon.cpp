// Texteingabe über die Systemtastatur der Konsole (swkbd-Applet).
//
// Flutter kennt auf dieser Plattform keine eingebaute Bildschirmtastatur:
// Das Framework meldet Fokus auf ein Textfeld über den Kanal
// `flutter/textinput` (JSONMethodCodec) und erwartet, dass die Plattform
// eine Tastatur zeigt und Änderungen als `TextInputClient.*`-Nachrichten
// zurückschickt.
//
// Die Switch hat dafür das swkbd-Applet: ein Vollbild-Dialog des Systems.
// `swkbdShow` blockiert, bis der Nutzer bestätigt oder abbricht - und weil
// es die Hauptschleife blockiert, steht währenddessen auch die Engine.
// Das ist hier kein Fehler, sondern die Natur des Applets: Es übernimmt
// den Bildschirm ohnehin komplett.
//
// Ablauf: setClient merkt sich Client-Kennung und Eingabeaktion,
// setEditingState den aktuellen Text. show öffnet das Applet mit diesem
// Text als Vorbelegung; nach Bestätigung gehen updateEditingState (neuer
// Text, Auswahl ans Ende) und performAction zurück. Abbruch lässt den
// alten Text stehen.
//
// Wichtig bei den Auswahl-Indizes: Flutter zählt in UTF-16-Einheiten,
// swkbd liefert UTF-8 - daher die Umrechnung in Utf16Length.

#include <switch.h>

#include <string.h>

#include <string>

#include "embedder.h"
#include "flutter_libnx/log.h"

namespace {

int g_client_id = -1;
std::string g_text;  // UTF-8, Stand des Frameworks
std::string g_input_action = "TextInputAction.done";

// Länge in UTF-16-Einheiten: Codepoints ab U+10000 zählen doppelt
// (Surrogatpaar), alle anderen einfach.
size_t Utf16Length(const std::string& utf8) {
  size_t units = 0;
  for (size_t i = 0; i < utf8.size();) {
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    size_t bytes = 1;
    if ((c & 0xF8) == 0xF0) {
      bytes = 4;
      units += 2;
    } else {
      if ((c & 0xE0) == 0xC0) {
        bytes = 2;
      } else if ((c & 0xF0) == 0xE0) {
        bytes = 3;
      }
      units += 1;
    }
    i += bytes;
  }
  return units;
}

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (const char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += ch;  // UTF-8 geht unverändert durch
        }
    }
  }
  return out;
}

void AppendUtf8(std::string* out, unsigned int cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// Holt den String hinter "key":"..." und löst JSON-Escapes auf
// (inkl. \uXXXX samt Surrogatpaaren). Reicht für den eigenen Codec des
// Frameworks; ein vollständiger JSON-Parser ist hier nicht nötig.
bool ExtractJsonString(const std::string& json,
                       const char* key,
                       std::string* out) {
  const std::string needle = std::string("\"") + key + "\":\"";
  const size_t start = json.find(needle);
  if (start == std::string::npos) {
    return false;
  }
  out->clear();
  size_t i = start + needle.size();
  while (i < json.size()) {
    const char c = json[i];
    if (c == '"') {
      return true;
    }
    if (c != '\\') {
      out->push_back(c);
      i++;
      continue;
    }
    if (i + 1 >= json.size()) {
      return false;
    }
    const char esc = json[i + 1];
    i += 2;
    switch (esc) {
      case 'n':
        out->push_back('\n');
        break;
      case 'r':
        out->push_back('\r');
        break;
      case 't':
        out->push_back('\t');
        break;
      case 'b':
        out->push_back('\b');
        break;
      case 'f':
        out->push_back('\f');
        break;
      case 'u': {
        if (i + 4 > json.size()) {
          return false;
        }
        unsigned int cp =
            static_cast<unsigned int>(strtoul(json.substr(i, 4).c_str(),
                                              nullptr, 16));
        i += 4;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= json.size() &&
            json[i] == '\\' && json[i + 1] == 'u') {
          const unsigned int low = static_cast<unsigned int>(
              strtoul(json.substr(i + 2, 4).c_str(), nullptr, 16));
          if (low >= 0xDC00 && low <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            i += 6;
          }
        }
        AppendUtf8(out, cp);
        break;
      }
      default:
        out->push_back(esc);  // \" \\ \/ und alles Unbekannte
    }
  }
  return false;
}

// Erste Zahl in "args":[N,...].
int ExtractClientId(const std::string& json) {
  const size_t start = json.find("\"args\":[");
  if (start == std::string::npos) {
    return -1;
  }
  return atoi(json.c_str() + start + 8);
}

void SendClientMessage(FLUTTER_API_SYMBOL(FlutterEngine) engine,
                       const std::string& payload) {
  FlutterPlatformMessage msg = {};
  msg.struct_size = sizeof(FlutterPlatformMessage);
  msg.channel = "flutter/textinput";
  msg.message = reinterpret_cast<const uint8_t*>(payload.data());
  msg.message_size = payload.size();
  FlutterEngineSendPlatformMessage(engine, &msg);
}

}  // namespace

extern "C" bool flutter_libnx_handle_textinput(
    FLUTTER_API_SYMBOL(FlutterEngine) engine,
    const FlutterPlatformMessage* message) {
  if (strcmp(message->channel, "flutter/textinput") != 0) {
    return false;
  }

  const std::string msg(reinterpret_cast<const char*>(message->message),
                        message->message_size);
  const auto respond_null = [&]() {
    if (message->response_handle != nullptr) {
      static const char kNull[] = "[null]";
      FlutterEngineSendPlatformMessageResponse(
          engine, message->response_handle,
          reinterpret_cast<const uint8_t*>(kNull), sizeof(kNull) - 1);
    }
  };

  if (msg.find("\"TextInput.setClient\"") != std::string::npos) {
    g_client_id = ExtractClientId(msg);
    if (!ExtractJsonString(msg, "inputAction", &g_input_action)) {
      g_input_action = "TextInputAction.done";
    }
    respond_null();
    return true;
  }

  if (msg.find("\"TextInput.setEditingState\"") != std::string::npos) {
    ExtractJsonString(msg, "text", &g_text);
    respond_null();
    return true;
  }

  if (msg.find("\"TextInput.clearClient\"") != std::string::npos) {
    g_client_id = -1;
    g_text.clear();
    respond_null();
    return true;
  }

  if (msg.find("\"TextInput.show\"") != std::string::npos) {
    // Erst quittieren, dann blockieren: Das Applet übernimmt gleich den
    // Bildschirm, die Antwort soll nicht so lange festhängen.
    respond_null();

    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
      LOG_ERROR("swkbdCreate fehlgeschlagen - keine Tastatur");
      return true;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetInitialText(&kbd, g_text.c_str());

    char out[2048] = {0};
    const Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);

    if (R_SUCCEEDED(rc) && g_client_id >= 0) {
      g_text.assign(out);
      const size_t sel = Utf16Length(g_text);
      SendClientMessage(
          engine,
          "{\"method\":\"TextInputClient.updateEditingState\",\"args\":[" +
              std::to_string(g_client_id) + ",{\"text\":\"" +
              JsonEscape(g_text) + "\",\"selectionBase\":" +
              std::to_string(sel) + ",\"selectionExtent\":" +
              std::to_string(sel) +
              ",\"selectionAffinity\":\"TextAffinity.downstream\","
              "\"selectionIsDirectional\":false,\"composingBase\":-1,"
              "\"composingExtent\":-1}]}");
      SendClientMessage(
          engine, "{\"method\":\"TextInputClient.performAction\",\"args\":[" +
                      std::to_string(g_client_id) + ",\"" + g_input_action +
                      "\"]}");
    }
    // Abbruch: alter Text bleibt, das Framework merkt nichts - genau wie
    // eine weggewischte Tastatur auf dem Handy.
    return true;
  }

  // hide, setEditableSizeAndTransform, setStyle, setCaretRect, ...:
  // zur Kenntnis genommen.
  respond_null();
  return true;
}
