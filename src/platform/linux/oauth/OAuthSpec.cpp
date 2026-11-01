#include "platform/linux/oauth/OAuthSpec.h"

#include <algorithm>
#include <cctype>

#include "core/script/ScriptMeta.h"
#include "core/net/Url.h"
#include "platform/linux/oauth/OAuthText.h"
#include "platform/linux/script/HeaderAttributes.h"

namespace awtrix::oauth {
namespace {

constexpr std::size_t kMaxUrl = 512;
constexpr std::size_t kMaxScope = 512;
constexpr std::size_t kMaxHosts = 8;
constexpr std::size_t kMaxParams = 8;

using Attr = script::HeaderAttribute;

bool tokenize(const std::string& s, std::vector<Attr>& out) {
  if (!script::splitAttributes(s, out)) return false;
  for (Attr& a : out) a.key = lowercase(a.key);
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
      const std::vector<std::string> hosts = script::splitHosts(a.value);
      spec.apiHosts.insert(spec.apiHosts.end(), hosts.begin(), hosts.end());
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
  if (!std::all_of(spec.apiHosts.begin(), spec.apiHosts.end(), script::validHost))
    return fail(error, "@oauth: bad api host");
  if (spec.scope.size() > kMaxScope || !printable(spec.scope)) return fail(error, "@oauth: scope too long");
  if (spec.params.size() > kMaxParams) return fail(error, "@oauth: too many params");
  return spec;
}

bool hostAllowed(const Spec& spec, const std::string& url) {
  return !spec.apiHosts.empty() && !script::urlRefusal(url, spec.apiHosts);
}

}
