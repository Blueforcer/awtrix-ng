#include "platform/linux/oauth/OAuthSpec.h"

#include <algorithm>
#include <cctype>

#include "core/script/ScriptMeta.h"
#include "core/net/Url.h"
#include "platform/linux/oauth/OAuthText.h"

namespace awtrix::oauth {
namespace {

constexpr std::size_t kMaxUrl = 512;
constexpr std::size_t kMaxScope = 512;
constexpr std::size_t kMaxHosts = 8;
constexpr std::size_t kMaxParams = 8;

struct Attr {
  std::string key;
  std::string value;
  bool hasValue = false;
};

bool tokenize(const std::string& s, std::vector<Attr>& out) {
  std::size_t i = 0;
  while (i < s.size()) {
    script::skipBlank<script::AttributeSyntax::OAuth>(s, i);
    if (i >= s.size()) break;
    Attr a;
    if (!script::readAttr<script::AttributeSyntax::OAuth>(s, i, a.key, a.value, &a.hasValue))
      return false;
    a.key = lowercase(a.key);
    out.push_back(std::move(a));
  }
  return true;
}

bool httpsUrl(const std::string& url) {
  const auto parsed = net::parseUrl(url);
  return url.size() <= kMaxUrl && printable(url) && parsed && parsed->tls() && !parsed->userinfo;
}

std::optional<Spec> fail(std::string* error, const char* message) {
  if (error) *error = message;
  return std::nullopt;
}

}

std::string hostOf(const std::string& url) {
  const auto parsed = net::parseUrl(url);
  return parsed && !parsed->userinfo ? lowercase(std::string(parsed->host)) : std::string();
}

std::optional<Spec> parseSpec(const std::string& source, std::string* error) {
  if (error) error->clear();
  std::vector<std::string> lines;
  script::forEachHeaderTag(source, [&](const std::string& tag, const std::string& value, int) {
    if (tag == "oauth") lines.push_back(value);
  });
  if (lines.empty()) return std::nullopt;
  if (lines.size() > 1) return fail(error, "only one @oauth line");
  std::vector<Attr> attrs;
  if (!tokenize(lines[0], attrs)) return fail(error, "@oauth: unterminated quote");
  Spec spec;
  spec.line = lines[0];
  for (const Attr& a : attrs) {
    if (a.key == "authorize" && a.hasValue) {
      spec.authorize = a.value;
    } else if (a.key == "token" && a.hasValue) {
      spec.token = a.value;
    } else if (a.key == "scope" && a.hasValue) {
      spec.scope = a.value;
    } else if (a.key == "pkce" && !a.hasValue) {
      spec.pkce = true;
    } else if (a.key == "auth" && a.hasValue && (lowercase(a.value) == "basic" || lowercase(a.value) == "body")) {
      spec.clientAuth = lowercase(a.value) == "body" ? ClientAuth::Body : ClientAuth::Basic;
    } else if (a.key == "api" && a.hasValue) {
      std::size_t pos = 0;
      for (;;) {
        const std::size_t comma = a.value.find(',', pos);
        const std::string host =
            lowercase(a.value.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        if (!host.empty()) spec.apiHosts.push_back(host);
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
    } else if (a.key == "params" && a.hasValue) {
      std::vector<Attr> pairs;
      if (!tokenize(a.value, pairs)) return fail(error, "@oauth: params must be key=value");
      for (const Attr& p : pairs) {
        if (!p.hasValue || p.key.empty()) return fail(error, "@oauth: params must be key=value");
        spec.params.emplace_back(p.key, p.value);
      }
    } else {
      return fail(error, "@oauth: unknown attribute");
    }
  }
  if (!httpsUrl(spec.authorize)) return fail(error, "@oauth: authorize must be an https URL");
  if (!httpsUrl(spec.token)) return fail(error, "@oauth: token must be an https URL");
  if (spec.apiHosts.empty() || spec.apiHosts.size() > kMaxHosts) return fail(error, "@oauth: api needs 1-8 hosts");
  for (const std::string& h : spec.apiHosts)
    if (h.size() > 253 || !printable(h) || h.find_first_of("/:@ ") != std::string::npos)
      return fail(error, "@oauth: bad api host");
  if (spec.scope.size() > kMaxScope || !printable(spec.scope)) return fail(error, "@oauth: scope too long");
  if (spec.params.size() > kMaxParams) return fail(error, "@oauth: too many params");
  return spec;
}

bool hostAllowed(const Spec& spec, const std::string& url) {
  const auto parsed = net::parseUrl(url);
  if (!parsed || !parsed->tls() || parsed->userinfo) return false;
  const std::string host = lowercase(std::string(parsed->host));
  return !host.empty() && std::find(spec.apiHosts.begin(), spec.apiHosts.end(), host) != spec.apiHosts.end();
}

}
