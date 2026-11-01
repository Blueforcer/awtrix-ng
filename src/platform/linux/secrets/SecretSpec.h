#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix::secrets {

struct Secret {
  std::string key;
  std::string label;
  std::string help;
  std::vector<std::string> hosts;
};

struct Spec {
  std::vector<Secret> secrets;

  const Secret* find(std::string_view key) const;
};

std::optional<Spec> parseSpec(const std::string& source, std::string* error = nullptr);
bool allows(const Secret& secret, const std::string& url, const char** reason = nullptr);

}
