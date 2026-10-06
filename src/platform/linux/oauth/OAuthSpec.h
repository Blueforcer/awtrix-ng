#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace awtrix::oauth {

enum class ClientAuth { Basic, Body };

// A script's `# @oauth` line: where it signs in, and the only hosts its token may go to.
struct Spec {
  std::string authorize;
  std::string token;
  std::vector<std::string> apiHosts;
  std::string scope;
  std::vector<std::pair<std::string, std::string>> params;
  bool pkce = false;
  ClientAuth clientAuth = ClientAuth::Basic;
  // The line as written after the tag; a sign-in is bound to it.
  std::string line;
};

// nullopt without an @oauth line; nullopt and a message in *error when it is malformed.
std::optional<Spec> parseSpec(const std::string& source, std::string* error = nullptr);

// An https URL whose host is one of the spec's api hosts.
bool hostAllowed(const Spec& spec, const std::string& url);

// The lowercase host of an http(s) URL, "" for anything else.
std::string hostOf(const std::string& url);

}
