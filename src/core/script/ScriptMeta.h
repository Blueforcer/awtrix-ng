#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace awtrix::script {

struct ScriptMeta {
  std::string name;
  std::string desc;
  std::string author;
  std::string version;
  bool headless = false;
  bool module = false;
  bool hasConfig = false;
  // Kept out of the rotation and loaded only while started from the on-device menu. Never set
  // together with module or headless.
  bool onDemand = false;
  std::string moduleName;
  std::string icons;
  // The usable @requires lines as "name" or "name/hub-id", one space between entries.
  std::string requirements;
  // The @needs capability names, lowercase, one space between entries.
  std::string needs;
  // The smallest panel from @display, or 0 x 0 when the script runs on any.
  int displayWidth = 0;
  int displayHeight = 0;
};

struct Requirement {
  std::string name;
  std::string hub;
};

struct StoredScript {
  std::string name;
  ScriptMeta meta;
};

namespace detail {

inline std::string trim(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  auto space = [](char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
  };
  while (b < e && space(s[b])) ++b;
  while (e > b && space(s[e - 1])) --e;
  return s.substr(b, e - b);
}

inline std::string lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

}

// Config headers accept an unfinished quote and blanks after '=' for compatibility.
// OAuth requires closed quotes and takes a blank after '=' as an empty value.
enum class AttributeSyntax { Config, OAuth };

template <AttributeSyntax Syntax = AttributeSyntax::Config>
inline bool attributeSpace(char c) {
  if constexpr (Syntax == AttributeSyntax::OAuth)
    return std::isspace(static_cast<unsigned char>(c)) != 0;
  return c == ' ' || c == '\t';
}

template <AttributeSyntax Syntax = AttributeSyntax::Config>
inline void skipBlank(const std::string& s, std::size_t& i) {
  const char* bytes = s.data();
  const std::size_t size = s.size();
  std::size_t at = i;
  while (at < size && attributeSpace<Syntax>(bytes[at])) ++at;
  i = at;
}

// Reads one bare or quoted word, preserving whether it was quoted.
template <AttributeSyntax Syntax = AttributeSyntax::Config>
inline bool readWord(const std::string& s, std::size_t& i, std::string& out, bool& quoted) {
  if constexpr (Syntax == AttributeSyntax::Config) skipBlank<Syntax>(s, i);
  out.clear();
  if (i >= s.size()) return false;
  quoted = s[i] == '"';
  if (quoted) ++i;
  const std::size_t start = i;
  const std::size_t size = s.size();
  const char* bytes = s.data();
  std::size_t at = i;
  if (quoted) {
    while (at < size && bytes[at] != '"') ++at;
  } else {
    while (at < size && !attributeSpace<Syntax>(bytes[at])) ++at;
  }
  i = at;
  out.assign(bytes + start, at - start);
  if (quoted) {
    if (at < size) ++i;
    else if constexpr (Syntax == AttributeSyntax::OAuth) return false;
    return true;
  }
  return !out.empty();
}

template <AttributeSyntax Syntax = AttributeSyntax::Config>
inline bool readAttr(const std::string& s, std::size_t& i, std::string& name,
                     std::string& value, bool* hasValue = nullptr) {
  skipBlank<Syntax>(s, i);
  if (i >= s.size()) return false;
  const std::size_t start = i;
  while (i < s.size() && !attributeSpace<Syntax>(s[i]) && s[i] != '=') ++i;
  name.assign(s, start, i - start);
  value.clear();
  const bool assigned = i < s.size() && s[i] == '=';
  if (hasValue) *hasValue = assigned;
  if (assigned) {
    ++i;
    bool quoted = false;
    const bool read = readWord<Syntax>(s, i, value, quoted);
    if constexpr (Syntax == AttributeSyntax::OAuth)
      if (quoted && !read) return false;
  }
  if constexpr (Syntax == AttributeSyntax::OAuth) return true;
  return !name.empty();
}

// Visits the `# @tag value` lines of a script header with (lowercased tag, value, 1-based
// line). Stops at the first line that is neither blank nor a comment -- the header must lead.
template <typename Fn>
inline void forEachHeaderTag(const std::string& source, Fn fn) {
  std::size_t pos = 0;
  int lineNo = 0;
  while (pos <= source.size()) {
    const std::size_t nl = source.find('\n', pos);
    std::string line =
        source.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    line = detail::trim(line);
    ++lineNo;

    if (line.empty()) {
    } else if (line[0] != '#') {
      break;
    } else {
      const std::string body = detail::trim(line.substr(1));
      if (!body.empty() && body[0] == '@') {
        const std::string rest = body.substr(1);
        const std::size_t sp = rest.find_first_of(" \t");
        fn(detail::lower(sp == std::string::npos ? rest : rest.substr(0, sp)),
           sp == std::string::npos ? std::string() : detail::trim(rest.substr(sp)), lineNo);
      }
    }

    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
}

constexpr std::size_t kIconsMax = 32;
constexpr std::size_t kIconIdMax = 32;

inline std::vector<std::string> splitIcons(const std::string& raw) {
  std::vector<std::string> out;
  const char* sep = " \t,";
  std::size_t pos = 0;

  while (out.size() < kIconsMax) {
    const std::size_t b = raw.find_first_not_of(sep, pos);
    if (b == std::string::npos) break;
    std::size_t e = raw.find_first_of(sep, b);
    if (e == std::string::npos) e = raw.size();
    const std::string id = raw.substr(b, e - b);
    pos = e;

    if (id.size() > kIconIdMax) continue;
    const bool ok = std::all_of(id.begin(), id.end(), [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
    });
    if (!ok) continue;
    if (std::find(out.begin(), out.end(), id) != out.end()) continue;

    out.push_back(id);
  }

  return out;
}

constexpr std::size_t kRequiresMax = 8;
constexpr std::size_t kRequireNameMax = 32;
constexpr std::size_t kHubIdLength = 12;

inline std::vector<Requirement> splitRequires(const std::string& raw) {
  std::vector<Requirement> out;
  std::size_t pos = 0;
  while (pos < raw.size()) {
    std::size_t end = raw.find(' ', pos);
    if (end == std::string::npos) end = raw.size();
    const std::string entry = raw.substr(pos, end - pos);
    pos = end + 1;
    if (entry.empty()) continue;
    const std::size_t slash = entry.find('/');
    out.push_back({entry.substr(0, slash), slash == std::string::npos ? std::string() : entry.substr(slash + 1)});
  }
  return out;
}

// One `@requires <name> [<hub-id>]` line, up to a `#`, which starts a comment. The name is a
// script's install name or a module's import name; a line whose name or Hub id is unusable is
// dropped whole, like a repeated name.
inline void addRequirement(std::string& list, const std::string& line) {
  const std::string value = line.substr(0, line.find('#'));
  const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-'; };
  const auto alnum = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; };
  const std::size_t split = value.find_first_of(" \t");
  const std::string name = value.substr(0, split);
  std::string hub;
  if (split != std::string::npos) {
    const std::size_t b = value.find_first_not_of(" \t", split);
    if (b != std::string::npos) hub = value.substr(b, value.find_first_of(" \t", b) - b);
  }
  if (name.empty() || name.size() > kRequireNameMax || !std::all_of(name.begin(), name.end(), word)) return;
  if (!hub.empty() && (hub.size() != kHubIdLength || !std::all_of(hub.begin(), hub.end(), alnum))) return;
  const std::vector<Requirement> have = splitRequires(list);
  if (have.size() >= kRequiresMax) return;
  for (const Requirement& r : have)
    if (r.name == name) return;
  if (!list.empty()) list += ' ';
  list += name;
  if (!hub.empty()) list += '/' + hub;
}

constexpr std::size_t kNeedsMax = 8;
constexpr std::size_t kNeedNameMax = 32;

// A capability name as GET /api/v1/capabilities spells it: lowercase words joined by dots.
inline bool isNeedName(const std::string& name) {
  if (name.empty() || name.size() > kNeedNameMax) return false;
  bool wordStart = true;
  for (char c : name) {
    const bool lower = c >= 'a' && c <= 'z';
    const bool digit = c >= '0' && c <= '9';
    if (wordStart) {
      if (!lower) return false;
      wordStart = false;
    } else if (c == '.') {
      wordStart = true;
    } else if (!lower && !digit) {
      return false;
    }
  }
  return !wordStart;
}

inline std::vector<std::string> splitNeeds(const std::string& raw) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (pos < raw.size()) {
    std::size_t end = raw.find(' ', pos);
    if (end == std::string::npos) end = raw.size();
    if (end > pos) out.push_back(raw.substr(pos, end - pos));
    pos = end + 1;
  }
  return out;
}

// One `@needs` line: capability names separated by spaces, tabs or commas, up to a `#`, which
// starts a comment. A malformed name is dropped and the rest of the line stays; names are
// lowercased, counted once and capped.
inline void addNeeds(std::string& list, const std::string& line) {
  const std::string value = line.substr(0, line.find('#'));
  std::vector<std::string> have = splitNeeds(list);
  const char* sep = " \t,";
  std::size_t pos = 0;
  while (have.size() < kNeedsMax) {
    const std::size_t b = value.find_first_not_of(sep, pos);
    if (b == std::string::npos) break;
    std::size_t e = value.find_first_of(sep, b);
    if (e == std::string::npos) e = value.size();
    const std::string name = detail::lower(value.substr(b, e - b));
    pos = e;
    if (!isNeedName(name) || std::find(have.begin(), have.end(), name) != have.end()) continue;
    have.push_back(name);
    if (!list.empty()) list += ' ';
    list += name;
  }
}

// `@display <width>x<height>`, the smallest panel the script is laid out for, up to a `#`, which
// starts a comment. Anything else is ignored, and a later usable line replaces an earlier one.
inline bool parseDisplay(const std::string& line, int& width, int& height) {
  const std::string value = detail::trim(line.substr(0, line.find('#')));
  auto digits = [&](std::size_t& i, int& out) {
    const std::size_t start = i;
    int v = 0;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9' && i - start < 3) v = v * 10 + (value[i++] - '0');
    if (i == start || (i < value.size() && value[i] >= '0' && value[i] <= '9')) return false;
    out = v;
    return true;
  };
  auto blanks = [&](std::size_t& i) {
    while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;
  };
  std::size_t i = 0;
  int w = 0;
  int h = 0;
  if (!digits(i, w)) return false;
  blanks(i);
  if (i >= value.size() || (value[i] != 'x' && value[i] != 'X')) return false;
  ++i;
  blanks(i);
  if (!digits(i, h) || i != value.size() || w < 1 || h < 1) return false;
  width = w;
  height = h;
  return true;
}

inline ScriptMeta parseMeta(const std::string& source) {
  ScriptMeta meta;

  forEachHeaderTag(source, [&meta](const std::string& key, const std::string& value, int) {
    // @module and @config are flags first: they count even with no value, where an empty
    // value on any other tag just means the author wrote the tag and nothing else.
    if (key == "module") {
      meta.module = true;
      meta.moduleName = value;
    } else if (key == "config") {
      meta.hasConfig = true;
    } else if (key == "ondemand") {
      meta.onDemand = true;
    } else if (value.empty()) {
    } else if (key == "name") {
      meta.name = value;
    } else if (key == "desc") {
      meta.desc = value;
    } else if (key == "author") {
      meta.author = value;
    } else if (key == "version") {
      meta.version = value;
    } else if (key == "headless") {
      const std::string v = detail::lower(value);
      meta.headless = v == "true" || v == "1" || v == "yes";
    } else if (key == "requires") {
      addRequirement(meta.requirements, value);
    } else if (key == "needs") {
      addNeeds(meta.needs, value);
    } else if (key == "oauth") {
      // A script that signs in needs a clock that can, whether or not @needs says so.
      addNeeds(meta.needs, "oauth");
    } else if (key == "display") {
      parseDisplay(value, meta.displayWidth, meta.displayHeight);
    } else if (key == "icons") {
      const std::string ids = detail::trim(value.substr(0, value.find('#')));
      const std::size_t room = kIconsMax * (kIconIdMax + 1);
      if (!ids.empty() && meta.icons.size() < room) {
        if (!meta.icons.empty()) meta.icons += ' ';
        meta.icons.append(ids, 0, room - meta.icons.size());
      }
    }
  });

  if (meta.module || meta.headless) meta.onDemand = false;
  return meta;
}

}
