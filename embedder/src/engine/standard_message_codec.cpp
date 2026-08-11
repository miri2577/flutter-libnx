// Umsetzung des StandardMessageCodec-Teilformats, siehe Header.
//
// Zwei Formregeln, die man leicht falsch macht:
//
//   * Größen sind dreistufig kodiert: < 254 als ein Byte, bis 0xFFFF als
//     254 + uint16, darüber als 255 + uint32 (jeweils little-endian).
//   * double wird auf ein Vielfaches von 8 relativ zum PUFFERANFANG
//     ausgerichtet, mit Nullbytes als Füllung - beim Lesen genauso.

#include "flutter_libnx/standard_message_codec.h"

#include <cstring>

namespace flutter_libnx {

namespace {

// Typbytes aus message_codecs.dart.
constexpr uint8_t kTypeNull = 0;
constexpr uint8_t kTypeTrue = 1;
constexpr uint8_t kTypeFalse = 2;
constexpr uint8_t kTypeInt32 = 3;
constexpr uint8_t kTypeInt64 = 4;
constexpr uint8_t kTypeFloat64 = 6;
constexpr uint8_t kTypeString = 7;
constexpr uint8_t kTypeList = 12;
constexpr uint8_t kTypeMap = 13;

void WriteSize(size_t n, std::vector<uint8_t>* out) {
  if (n < 254) {
    out->push_back(static_cast<uint8_t>(n));
  } else if (n <= 0xFFFF) {
    out->push_back(254);
    out->push_back(static_cast<uint8_t>(n & 0xFF));
    out->push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
  } else {
    out->push_back(255);
    for (int i = 0; i < 4; i++) {
      out->push_back(static_cast<uint8_t>((n >> (8 * i)) & 0xFF));
    }
  }
}

bool ReadSize(const uint8_t* data, size_t size, size_t* cursor, size_t* out) {
  if (*cursor >= size) {
    return false;
  }
  const uint8_t first = data[(*cursor)++];
  if (first < 254) {
    *out = first;
    return true;
  }
  const int bytes = (first == 254) ? 2 : 4;
  if (*cursor + bytes > size) {
    return false;
  }
  size_t value = 0;
  for (int i = 0; i < bytes; i++) {
    value |= static_cast<size_t>(data[*cursor + i]) << (8 * i);
  }
  *cursor += bytes;
  *out = value;
  return true;
}

void AlignTo(size_t alignment, std::vector<uint8_t>* out) {
  while (out->size() % alignment != 0) {
    out->push_back(0);
  }
}

bool SkipAlignment(size_t alignment, size_t size, size_t* cursor) {
  while (*cursor % alignment != 0) {
    if (*cursor >= size) {
      return false;
    }
    (*cursor)++;
  }
  return true;
}

}  // namespace

StdValue StdValue::Bool(bool v) {
  StdValue r;
  r.type = Type::kBool;
  r.as_bool = v;
  return r;
}

StdValue StdValue::Int(int64_t v) {
  StdValue r;
  r.type = Type::kInt;
  r.as_int = v;
  return r;
}

StdValue StdValue::Double(double v) {
  StdValue r;
  r.type = Type::kDouble;
  r.as_double = v;
  return r;
}

StdValue StdValue::String(std::string v) {
  StdValue r;
  r.type = Type::kString;
  r.as_string = std::move(v);
  return r;
}

StdValue StdValue::List() {
  StdValue r;
  r.type = Type::kList;
  return r;
}

StdValue StdValue::Map() {
  StdValue r;
  r.type = Type::kMap;
  return r;
}

void EncodeStdValue(const StdValue& value, std::vector<uint8_t>* out) {
  switch (value.type) {
    case StdValue::Type::kNull:
      out->push_back(kTypeNull);
      break;
    case StdValue::Type::kBool:
      out->push_back(value.as_bool ? kTypeTrue : kTypeFalse);
      break;
    case StdValue::Type::kInt:
      if (value.as_int >= INT32_MIN && value.as_int <= INT32_MAX) {
        out->push_back(kTypeInt32);
        for (int i = 0; i < 4; i++) {
          out->push_back(
              static_cast<uint8_t>((value.as_int >> (8 * i)) & 0xFF));
        }
      } else {
        out->push_back(kTypeInt64);
        for (int i = 0; i < 8; i++) {
          out->push_back(
              static_cast<uint8_t>((value.as_int >> (8 * i)) & 0xFF));
        }
      }
      break;
    case StdValue::Type::kDouble: {
      out->push_back(kTypeFloat64);
      AlignTo(8, out);
      uint64_t bits = 0;
      std::memcpy(&bits, &value.as_double, sizeof(bits));
      for (int i = 0; i < 8; i++) {
        out->push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xFF));
      }
      break;
    }
    case StdValue::Type::kString:
      out->push_back(kTypeString);
      WriteSize(value.as_string.size(), out);
      out->insert(out->end(), value.as_string.begin(), value.as_string.end());
      break;
    case StdValue::Type::kList:
      out->push_back(kTypeList);
      WriteSize(value.as_list.size(), out);
      for (const StdValue& item : value.as_list) {
        EncodeStdValue(item, out);
      }
      break;
    case StdValue::Type::kMap:
      out->push_back(kTypeMap);
      WriteSize(value.as_map.size(), out);
      for (const auto& entry : value.as_map) {
        EncodeStdValue(entry.first, out);
        EncodeStdValue(entry.second, out);
      }
      break;
  }
}

bool DecodeStdValue(const uint8_t* data,
                    size_t size,
                    size_t* cursor,
                    StdValue* out) {
  if (*cursor >= size) {
    return false;
  }
  const uint8_t type = data[(*cursor)++];
  switch (type) {
    case kTypeNull:
      *out = StdValue::Null();
      return true;
    case kTypeTrue:
      *out = StdValue::Bool(true);
      return true;
    case kTypeFalse:
      *out = StdValue::Bool(false);
      return true;
    case kTypeInt32: {
      if (*cursor + 4 > size) {
        return false;
      }
      int32_t v = 0;
      std::memcpy(&v, data + *cursor, 4);
      *cursor += 4;
      *out = StdValue::Int(v);
      return true;
    }
    case kTypeInt64: {
      if (*cursor + 8 > size) {
        return false;
      }
      int64_t v = 0;
      std::memcpy(&v, data + *cursor, 8);
      *cursor += 8;
      *out = StdValue::Int(v);
      return true;
    }
    case kTypeFloat64: {
      if (!SkipAlignment(8, size, cursor) || *cursor + 8 > size) {
        return false;
      }
      double v = 0;
      std::memcpy(&v, data + *cursor, 8);
      *cursor += 8;
      *out = StdValue::Double(v);
      return true;
    }
    case kTypeString: {
      size_t len = 0;
      if (!ReadSize(data, size, cursor, &len) || *cursor + len > size) {
        return false;
      }
      *out = StdValue::String(
          std::string(reinterpret_cast<const char*>(data + *cursor), len));
      *cursor += len;
      return true;
    }
    case kTypeList: {
      size_t count = 0;
      if (!ReadSize(data, size, cursor, &count)) {
        return false;
      }
      StdValue list = StdValue::List();
      list.as_list.reserve(count);
      for (size_t i = 0; i < count; i++) {
        StdValue item;
        if (!DecodeStdValue(data, size, cursor, &item)) {
          return false;
        }
        list.as_list.push_back(std::move(item));
      }
      *out = std::move(list);
      return true;
    }
    case kTypeMap: {
      size_t count = 0;
      if (!ReadSize(data, size, cursor, &count)) {
        return false;
      }
      StdValue map = StdValue::Map();
      map.as_map.reserve(count);
      for (size_t i = 0; i < count; i++) {
        StdValue key;
        StdValue value;
        if (!DecodeStdValue(data, size, cursor, &key) ||
            !DecodeStdValue(data, size, cursor, &value)) {
          return false;
        }
        map.as_map.emplace_back(std::move(key), std::move(value));
      }
      *out = std::move(map);
      return true;
    }
    default:
      // Typisierte Arrays (8-11, 14) und Alt-Formate absichtlich nicht
      // verstanden - siehe Kopf des Headers.
      return false;
  }
}

bool DecodeStdMethodCall(const uint8_t* data,
                         size_t size,
                         std::string* method,
                         StdValue* args) {
  size_t cursor = 0;
  StdValue name;
  if (!DecodeStdValue(data, size, &cursor, &name) ||
      name.type != StdValue::Type::kString) {
    return false;
  }
  if (!DecodeStdValue(data, size, &cursor, args)) {
    return false;
  }
  *method = std::move(name.as_string);
  return true;
}

std::vector<uint8_t> EncodeStdSuccess(const StdValue& result) {
  std::vector<uint8_t> out;
  out.push_back(0);
  EncodeStdValue(result, &out);
  return out;
}

std::vector<uint8_t> EncodeStdError(const std::string& code,
                                    const std::string& message) {
  std::vector<uint8_t> out;
  out.push_back(1);
  EncodeStdValue(StdValue::String(code), &out);
  EncodeStdValue(StdValue::String(message), &out);
  EncodeStdValue(StdValue::Null(), &out);
  return out;
}

}  // namespace flutter_libnx
