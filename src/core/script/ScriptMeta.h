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
  std::string moduleName;
  std::string icons;
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
    } else if (key == "icons") {
      // Gekappt beim Anhaengen, nicht erst beim Auswerten: der rohe Text wird je
      // Skript gehalten und bei jedem /api/v1/apps kopiert. Was ueber das
      // Fassungsvermoegen der Liste hinausgeht, kann splitIcons ohnehin nicht
      // mehr ausgeben - es wuerde nur Heap belegen.
      const std::size_t room = kIconsMax * (kIconIdMax + 1);
      if (meta.icons.size() < room) {
        if (!meta.icons.empty()) meta.icons += ' ';
        meta.icons.append(value, 0, room - meta.icons.size());
      }
    }
  });

  return meta;
}

}
