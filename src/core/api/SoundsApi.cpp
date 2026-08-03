#include "core/api/SoundsApi.h"

#include <string>
#include <string_view>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/sound/Rtttl.h"

namespace awtrix {
namespace api {
namespace sounds {

namespace {

constexpr const char* kDir = "/MELODIES/";
constexpr const char* kExt = ".txt";

PutResult reject(int status, const char* code, const std::string& message,
                 const char* field) {
  PutResult r;
  r.ok = false;
  r.status = status;
  r.code = code;
  r.message = message;
  r.field = field;
  return r;
}

}

std::string nameFromFile(const std::string& fileName) {
  const size_t slash = fileName.find_last_of('/');
  const std::string base =
      slash == std::string::npos ? fileName : fileName.substr(slash + 1);
  const size_t extLen = 4;
  if (base.size() <= extLen) return "";
  if (base.compare(base.size() - extLen, extLen, kExt) != 0) return "";
  const std::string stem = base.substr(0, base.size() - extLen);
  return rtttl::validName(stem) ? stem : "";
}

std::string pathFor(const std::string& name) { return std::string(kDir) + name + kExt; }

std::string entryJson(const std::string& name, const std::string& content, uint32_t bytes) {
  const rtttl::Parse p = rtttl::parse(content);

  std::string out;
  out.reserve(content.size() + 160);
  JsonWriter w(out);
  w.beginObject();
  w.member("name", name);
  w.member("rtttl", content);
  w.member("bytes", bytes);
  w.member("notes", static_cast<uint32_t>(p.notes.size()));
  w.member("durationMs", p.durationMs());
  w.member("valid", p.ok);
  if (!p.ok) {
    w.member("error", p.error);
    w.member("index", static_cast<uint32_t>(p.index));
  }
  w.endObject();
  return out;
}

// Rewrites the melody's own title to match the file name, so the two can never disagree.
PutResult prepareWrite(const std::string& name, const std::string& body) {
  if (!rtttl::validName(name))
    return reject(422, "validationFailed",
                  "melody name must be 1 to 24 characters of A-Z, a-z, 0-9, _ or -", "name");

  if (!isWellFormed(body))
    return reject(400, "invalidJson", "request body is not valid JSON", "rtttl");

  std::string in;
  if (!memberValue(JsonReader(body), "rtttl").appendString(in))
    return reject(422, "validationFailed", "\"rtttl\" is required and must be a string", "rtttl");

  std::string titled;
  if (!rtttl::retitle(in, name, titled))
    return reject(422, "validationFailed",
                  "a melody is \"defaults:notes\", for example \"d=4,o=5,b=120:c,e,g\"", "rtttl");

  const rtttl::Parse p = rtttl::parse(titled);
  if (!p.ok) return reject(422, "validationFailed", p.describe(), "rtttl");

  PutResult r;
  r.ok = true;
  r.status = 200;
  r.content = titled;
  return r;
}

}
}
}
