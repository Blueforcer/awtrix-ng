#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace awtrix {
namespace api {

// Cursor-style pull parser over the raw text: nothing is allocated up front. Copying a reader
// snapshots its position, which is how callers bookmark a value and come back to it later.
class JsonReader {
 public:
  enum class Type : uint8_t { Invalid, Null, Bool, Number, String, Object, Array };

  JsonReader() = default;

  explicit JsonReader(std::string_view text) : p_(text.data()), end_(text.data() + text.size()) {
    skipSpace();
  }

  bool ok() const { return ok_; }

  Type type() const;
  bool isNull() const { return type() == Type::Null; }
  bool isBool() const { return type() == Type::Bool; }
  bool isNumber() const { return type() == Type::Number; }
  bool isString() const { return type() == Type::String; }
  bool isObject() const { return type() == Type::Object; }
  bool isArray() const { return type() == Type::Array; }

  bool isInteger() const;

  bool asBool(bool& out) const;
  bool asLong(long long& out) const;
  bool asDouble(double& out) const;

  bool appendString(std::string& out) const;

  // Zero-copy view of the string, empty if it contains any escape; use appendString for those.
  std::string_view rawString() const;

  bool stringView(std::string_view& out) const;

  bool skipValue();

  std::string_view valueText() const;

  bool enterObject();
  bool nextMember();
  std::string_view key() const { return key_; }
  bool keyEquals(const char* name) const;

  bool atEnd() const { return p_ >= end_; }

  bool enterArray();
  bool nextElement();

 private:
  void skipSpace();
  bool skipString();
  bool skipNumber();
  bool literal(const char* word);
  const char* valueEnd() const;

  const char* p_ = nullptr;
  const char* end_ = nullptr;
  std::string_view key_;
  bool ok_ = true;
  int depth_ = 0;
  // Nesting cap: skipValue() gives up instead of recursing without bound.
  static constexpr int kMaxDepth = 16;
  bool first_ = false;
};

JsonReader memberValue(JsonReader obj, const char* name);

// A missing member reads back as Type::Invalid, so this answers "was the key in the object".
inline bool present(const JsonReader& r) { return r.type() != JsonReader::Type::Invalid; }

bool isWellFormed(std::string_view text);

struct Member {
  const char* key;
  JsonReader* value;
};

bool readMembers(std::string_view text, std::initializer_list<Member> members);

bool parseDouble(const char* begin, const char* end, double& out);

}
}
