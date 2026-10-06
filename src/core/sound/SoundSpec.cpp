#include "core/sound/SoundSpec.h"

#include <utility>

#include "core/api/JsonReader.h"
#include "core/sound/AudioSinks.h"
#include "core/sound/Rtttl.h"
#include "core/sound/SoundMp3.h"

namespace awtrix {
namespace sound {
namespace {

constexpr const char* kSourceKeys[] = {"file", "rtttl", "song", "speech", "track", "station"};
constexpr std::size_t kSourceCount = sizeof(kSourceKeys) / sizeof(kSourceKeys[0]);

std::string join(const std::string& prefix, std::string_view key) {
  if (prefix.empty()) return std::string(key);
  std::string out = prefix;
  out += '.';
  out.append(key.data(), key.size());
  return out;
}

// "<prefix>[<index>]"; a list has at most kMaxChoices entries, so the index is one digit.
std::string indexed(const std::string& prefix, int index) {
  std::string out = prefix;
  out += '[';
  out += static_cast<char>('0' + index);
  out += ']';
  return out;
}

bool fail(DispatchDetail& d, std::string field, const char* message) {
  d.field = std::move(field);
  d.message = message;
  return false;
}

// A name, "Script/name", or an address; a name holds neither '/' nor ':', so these cannot mix.
bool validFile(const std::string& value) {
  if (isUrl(value)) return true;
  const std::size_t slash = value.find('/');
  if (slash == std::string::npos) return validName(value);
  return validName(value.substr(0, slash)) && validName(value.substr(slash + 1));
}

bool readObject(api::JsonReader r, Origin origin, const std::string& prefix, bool inList,
                Spec& out, DispatchDetail& d) {
  api::JsonReader at[kSourceCount];
  api::JsonReader atLoop, atNextBar;
  api::JsonReader it = r;
  if (!it.enterObject()) return fail(d, prefix, "must be a string, object or list");
  while (it.nextMember()) {
    const std::string_view k = it.key();
    bool known = false;
    for (std::size_t i = 0; i < kSourceCount && !known; ++i) {
      if (k == kSourceKeys[i]) {
        at[i] = it;
        known = true;
      }
    }
    if (!known && k == "loop") {
      atLoop = it;
      known = true;
    }
    if (!known && k == "nextBar") {
      atNextBar = it;
      known = true;
    }
    if (!known) return fail(d, join(prefix, k), "unknown field");
    if (!it.skipValue()) return fail(d, prefix, "invalid JSON");
  }

  std::size_t found = kSourceCount;
  int count = 0;
  for (std::size_t i = 0; i < kSourceCount; ++i) {
    if (!api::present(at[i])) continue;
    if (count == 0) found = i;
    ++count;
  }
  if (count == 0) return fail(d, prefix, "needs a sound key");
  if (count > 1) return fail(d, join(prefix, kSourceKeys[found]), "one sound key only");

  out = Spec{};
  out.kind = static_cast<Kind>(found);
  const std::string field = join(prefix, kSourceKeys[found]);
  const api::JsonReader& v = at[found];
  switch (out.kind) {
    case Kind::File:
      if (!v.isString() || !v.appendString(out.text) || !validFile(out.text))
        return fail(d, field, "invalid name");
      break;
    case Kind::Rtttl: {
      if (!v.isString()) return fail(d, field, "must be a string");
      v.appendString(out.text);
      const rtttl::Parse parsed = rtttl::parse(out.text);
      if (!parsed.ok) {
        d.field = field;
        d.message = parsed.describe();
        return false;
      }
      break;
    }
    case Kind::Song:
      if (!v.isString() || !v.appendString(out.text) || out.text.empty())
        return fail(d, field, "must be song text");
      break;
    case Kind::Speech:
      if (!v.isString() || !v.appendString(out.text) || out.text.empty() ||
          out.text.size() > kMaxSpeechBytes)
        return fail(d, field, "must be 1..512 bytes");
      break;
    case Kind::Track: {
      long long n = 0;
      if (!v.isNumber() || !v.isInteger() || !v.asLong(n) || n < kMinTrack || n > kMaxTrack)
        return fail(d, field, "must be 1..2999");
      out.number = static_cast<int>(n);
      break;
    }
    case Kind::Station: {
      if (inList || origin == Origin::Notification) return fail(d, field, "not here");
      long long n = 0;
      if (v.isString()) {
        v.appendString(out.text);
        if (out.text.empty()) return fail(d, field, "expected name, position or address");
      } else if (v.isNumber() && v.isInteger() && v.asLong(n) && n >= 0 && n < 1000) {
        out.number = static_cast<int>(n);
      } else {
        return fail(d, field, "expected name, position or address");
      }
      break;
    }
  }

  if (api::present(atLoop)) {
    if (!atLoop.isBool() || !atLoop.asBool(out.loop))
      return fail(d, join(prefix, "loop"), "must be true or false");
    if (out.kind == Kind::Station) return fail(d, join(prefix, "loop"), "not with station");
  }
  if (api::present(atNextBar)) {
    if (!atNextBar.isBool() || !atNextBar.asBool(out.nextBar))
      return fail(d, join(prefix, "nextBar"), "must be true or false");
    if (origin != Origin::Script || out.kind != Kind::Song || !out.loop)
      return fail(d, join(prefix, "nextBar"), "only with a looping song");
  }
  return true;
}

bool readEntry(api::JsonReader r, Origin origin, const std::string& prefix, bool inList,
               Spec& out, DispatchDetail& d) {
  if (r.isString()) {
    out = Spec{};
    r.appendString(out.text);
    if (!validFile(out.text)) return fail(d, join(prefix, "file"), "invalid name");
    return true;
  }
  if (!r.isObject()) return fail(d, prefix, "must be a string, object or list");
  return readObject(r, origin, prefix, inList, out, d);
}

}

std::string specField(Origin origin, int index, const char* key) {
  const std::string prefix = origin == Origin::Notification ? "sound" : "";
  return join(index >= 0 ? indexed(prefix, index) : prefix, key);
}

bool parse(std::string_view json, Origin origin, Choices& out, DispatchDetail& d) {
  out.count = 0;
  const std::string prefix = origin == Origin::Notification ? "sound" : "";
  if (!api::isWellFormed(json)) return fail(d, prefix, "invalid JSON");
  api::JsonReader r(json);
  if (!r.isArray()) {
    if (!readEntry(r, origin, prefix, false, out.items[0], d)) return false;
    out.count = 1;
    return true;
  }
  api::JsonReader it = r;
  if (!it.enterArray()) return fail(d, prefix, "must have 1 to 4 entries");
  while (it.nextElement()) {
    if (out.count == kMaxChoices) return fail(d, prefix, "must have 1 to 4 entries");
    if (!readEntry(it, origin, indexed(prefix, out.count), true, out.items[out.count], d))
      return false;
    ++out.count;
    if (!it.skipValue()) return fail(d, prefix, "invalid JSON");
  }
  if (out.count == 0) return fail(d, prefix, "must have 1 to 4 entries");
  return true;
}

std::string displayName(const Spec& spec) {
  switch (spec.kind) {
    case Kind::File:
      return spec.text;
    case Kind::Rtttl:
      return "rtttl";
    case Kind::Song:
      return "song";
    case Kind::Speech:
      return "speech";
    case Kind::Track:
      return std::to_string(spec.number);
    case Kind::Station:
      return spec.text.empty() ? std::to_string(spec.number) : spec.text;
  }
  return std::string();
}

}
}
