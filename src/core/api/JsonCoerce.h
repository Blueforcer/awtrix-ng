#pragma once

#include <limits>
#include <string>

#include "core/api/JsonReader.h"

namespace awtrix {
namespace api {


inline bool stringAsNumber(JsonReader r, double& out) {
  std::string s;
  if (!r.appendString(s) || s.empty()) return false;
  for (char c : s) {
    const bool numeric =
        (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
    if (!numeric) return false;
  }
  return parseDouble(s.data(), s.data() + s.size(), out);
}

// Truthiness for lenient fields: numbers by zero test, and any other present, non-null value
// counts as true, including strings, objects and arrays.
inline bool coerceBool(JsonReader r) {
  bool b = false;
  if (r.asBool(b)) return b;
  if (r.isNumber()) {
    double d = 0.0;
    return r.asDouble(d) && d != 0.0;
  }
  return !r.isNull() && r.type() != JsonReader::Type::Invalid;
}

inline bool coerceNumber(JsonReader r, double& out) {
  bool b = false;
  if (r.asBool(b)) {
    out = b ? 1.0 : 0.0;
    return true;
  }
  if (r.isNumber()) return r.asDouble(out);
  if (r.isString()) return stringAsNumber(r, out);
  return false;
}

inline float coerceFloat(JsonReader r) {
  double d = 0.0;
  return coerceNumber(r, d) ? static_cast<float>(d) : 0.0f;
}

// Values outside the range of T, and anything unparseable, come back as 0 rather than clamped.
template <typename T>
T coerceInt(JsonReader r) {
  constexpr long long lo = static_cast<long long>(std::numeric_limits<T>::min());
  constexpr long long hi = static_cast<long long>(std::numeric_limits<T>::max());
  if (r.isNumber() && r.isInteger()) {
    long long v = 0;
    if (!r.asLong(v)) return 0;
    return (v >= lo && v <= hi) ? static_cast<T>(v) : 0;
  }
  double d = 0.0;
  if (!coerceNumber(r, d)) return 0;
  if (!(d >= static_cast<double>(lo) && d < static_cast<double>(hi) + 1.0)) return 0;
  return static_cast<T>(d);
}

}
}
