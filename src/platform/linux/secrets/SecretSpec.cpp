#include "platform/linux/secrets/SecretSpec.h"

#include <algorithm>
#include <utility>

#include "core/StrCase.h"
#include "core/script/ScriptMeta.h"
#include "platform/linux/net/PrivateHost.h"
#include "platform/linux/net/UrlPolicy.h"
#include "platform/linux/script/HeaderAttributes.h"

namespace awtrix::secrets {
namespace {

constexpr std::size_t kMaxSecrets = 4;
constexpr std::size_t kMaxKey = 24;
constexpr std::size_t kMaxLabel = 48;
constexpr std::size_t kMaxHelp = 96;
constexpr std::size_t kMaxHosts = 8;

bool validKey(const std::string& key) {
  if (key.empty() || key.size() > kMaxKey) return false;
  for (std::size_t i = 0; i < key.size(); ++i) {
    const char c = key[i];
    const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    if (!letter && (i == 0 || c < '0' || c > '9')) return false;
  }
  return true;
}

const char* parseLine(const std::string& line, Secret& out) {
  std::vector<script::HeaderAttribute> attributes;
  if (!script::splitAttributes(line, attributes)) return "@secret: unterminated quote";
  if (attributes.empty() || attributes[0].hasValue || !validKey(attributes[0].key)) return "@secret: bad key";
  out.key = attributes[0].key;
  for (auto a = attributes.begin() + 1; a != attributes.end(); ++a) {
    const std::string name = strcase::toLower(a->key);
    if (name == "label" && a->hasValue) {
      out.label = a->value;
    } else if (name == "help" && a->hasValue) {
      out.help = a->value;
    } else if (name == "hosts" && a->hasValue) {
      out.hosts = net::splitHosts(a->value);
    } else {
      return "@secret: unknown attribute";
    }
  }
  if (out.label.size() > kMaxLabel) return "@secret: label too long";
  if (out.help.size() > kMaxHelp) return "@secret: help too long";
  if (out.hosts.empty() || out.hosts.size() > kMaxHosts) return "@secret: hosts needs 1-8 hosts";
  if (!std::all_of(out.hosts.begin(), out.hosts.end(), net::validHost)) return "@secret: bad host";
  std::sort(out.hosts.begin(), out.hosts.end());
  out.hosts.erase(std::unique(out.hosts.begin(), out.hosts.end()), out.hosts.end());
  return nullptr;
}

}

const Secret* Spec::find(std::string_view key) const {
  const auto it = std::find_if(secrets.begin(), secrets.end(), [key](const Secret& s) { return s.key == key; });
  return it == secrets.end() ? nullptr : &*it;
}

std::optional<Spec> parseSpec(const std::string& source, std::string* error) {
  if (error) error->clear();
  const std::vector<std::string> lines = script::headerLines(source, "secret");
  if (lines.empty()) return std::nullopt;
  if (lines.size() > kMaxSecrets) return script::specFailure(error, "@secret: at most 4 lines");
  Spec spec;
  for (const std::string& line : lines) {
    Secret secret;
    if (const char* problem = parseLine(line, secret)) return script::specFailure(error, problem);
    if (spec.find(secret.key)) return script::specFailure(error, "@secret: key declared twice");
    spec.secrets.push_back(std::move(secret));
  }
  return spec;
}

bool allows(const Secret& secret, const std::string& url, const char** reason) {
  const char* refusal = net::urlRefusal(url, secret.hosts, net::isPrivateHost);
  if (reason) *reason = refusal;
  return !refusal;
}

}
