// StandardMessageCodec - das Binärformat der Flutter-Plugin-Kanäle.
//
// Die Kanäle unter plugins.flutter.io/* sprechen nicht JSON, sondern den
// StandardMethodCodec: ein Methodenaufruf ist zwei direkt hintereinander
// kodierte Werte (Name als String, dann die Argumente), eine Antwort ist ein
// Umschlagbyte (0 = Erfolg, 1 = Fehler) gefolgt von den kodierten Werten.
//
// Hier steht bewusst nur die Teilmenge, die path_provider und
// shared_preferences brauchen: null, bool, int32/64, double, String, List,
// Map. Die typisierten Arrays (Uint8List usw.) lehnt der Decoder ehrlich ab,
// statt sie halb zu verstehen - wer sie braucht, sieht sofort wo es klemmt.
//
// Formatreferenz: flutter/lib/src/services/message_codecs.dart
// (StandardMessageCodec.writeValue/readValue).

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace flutter_libnx {

struct StdValue {
  enum class Type { kNull, kBool, kInt, kDouble, kString, kList, kMap };

  Type type = Type::kNull;
  bool as_bool = false;
  int64_t as_int = 0;
  double as_double = 0.0;
  std::string as_string;
  std::vector<StdValue> as_list;
  // Reihenfolge bleibt erhalten; für die Handvoll Einträge der Plugins ist
  // lineare Suche billiger als jede Baumstruktur.
  std::vector<std::pair<StdValue, StdValue>> as_map;

  static StdValue Null() { return StdValue{}; }
  static StdValue Bool(bool v);
  static StdValue Int(int64_t v);
  static StdValue Double(double v);
  static StdValue String(std::string v);
  static StdValue List();
  static StdValue Map();
};

// Hängt die Kodierung von value an out an.
void EncodeStdValue(const StdValue& value, std::vector<uint8_t>* out);

// Liest ab *cursor einen Wert. false bei unbekanntem Typbyte oder
// abgeschnittenen Daten; *cursor steht danach hinter dem gelesenen Wert.
bool DecodeStdValue(const uint8_t* data,
                    size_t size,
                    size_t* cursor,
                    StdValue* out);

// Methodenaufruf eines StandardMethodCodec-Kanals: Name, dann Argumente.
bool DecodeStdMethodCall(const uint8_t* data,
                         size_t size,
                         std::string* method,
                         StdValue* args);

// Antwort-Umschläge.
std::vector<uint8_t> EncodeStdSuccess(const StdValue& result);
std::vector<uint8_t> EncodeStdError(const std::string& code,
                                    const std::string& message);

}  // namespace flutter_libnx
