#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace awtrix::posix {

class PrivateStore {
 public:
  PrivateStore(std::string directory, std::size_t maxBytes);

  bool load(const std::string& name, std::string& text) const;
  bool save(const std::string& name, std::string_view text);
  bool erase(const std::string& name);
  bool eraseAll();
  void keepOnly(const std::function<bool(const std::string& text)>& keep);

 private:
  std::string path(const std::string& name) const;

  std::string dir_;
  std::size_t maxBytes_;
};

}
