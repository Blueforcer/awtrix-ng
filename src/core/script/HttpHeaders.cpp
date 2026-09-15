#include "core/script/HttpHeaders.h"

#include <cctype>

#include "core/script/ScriptServices.h"

namespace awtrix::script {
namespace {

const char* const kMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};

// Framing headers the transport must own. A script setting these could desynchronise the
// request from what is actually sent, so they are dropped rather than passed on.
const char* const kBlockedHeaders[] = {"host", "content-length", "transfer-encoding",
                                       "connection"};

char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalsFold(const std::string& a, const char* b) {
  std::size_t i = 0;
  for (; i < a.size(); ++i) {
    if (b[i] == '\0' || lower(a[i]) != b[i]) return false;
  }
  return b[i] == '\0';
}

std::string trim(const std::string& s, std::size_t from, std::size_t to) {
  while (from < to && (s[from] == ' ' || s[from] == '\t')) ++from;
  while (to > from && (s[to - 1] == ' ' || s[to - 1] == '\t')) --to;
  return s.substr(from, to - from);
}

bool nameIsToken(const std::string& n) {
  if (n.empty()) return false;
  for (const char c : n) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u >= 0x7f) return false;
    if (c == ':' || c == ',' || c == ';' || c == '(' || c == ')' || c == '<' || c == '>' ||
        c == '@' || c == '"' || c == '/' || c == '[' || c == ']' || c == '?' || c == '=' ||
        c == '{' || c == '}' || c == '\\')
      return false;
  }
  return true;
}

bool valueIsClean(const std::string& v) {
  for (const char c : v) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f) return false;
  }
  return true;
}

}

bool normalizeMethod(const std::string& in, std::string& out) {
  if (in.empty()) {
    out = "GET";
    return true;
  }
  std::string up;
  up.reserve(in.size());
  for (const char c : in) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  for (const char* m : kMethods) {
    if (up == m) {
      out = up;
      return true;
    }
  }
  return false;
}

bool headerAllowed(const std::string& name) {
  for (const char* b : kBlockedHeaders)
    if (equalsFold(name, b)) return false;
  return true;
}

// Note the asymmetry: a blocked header is skipped and the rest still goes through, while a
// malformed one fails the whole block. `out` is left untouched on failure.
bool parseHeaderBlock(const std::string& block, HttpHeaders& out) {
  HttpHeaders parsed;
  std::size_t pos = 0;

  while (pos < block.size()) {
    std::size_t end = block.find(kHeaderSeparator, pos);
    if (end == std::string::npos) end = block.size();

    const std::string line = trim(block, pos, end);
    pos = end + 1;
    if (line.empty()) continue;

    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) return false;

    const std::string name = trim(line, 0, colon);
    const std::string value = trim(line, colon + 1, line.size());
    if (!nameIsToken(name) || !valueIsClean(value)) return false;
    if (!headerAllowed(name)) continue;

    parsed.emplace_back(name, value);
  }

  out = std::move(parsed);
  return true;
}

}
