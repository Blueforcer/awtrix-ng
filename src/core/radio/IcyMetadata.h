#pragma once

#include <string>

namespace awtrix {
namespace radio {


bool parseStreamTitle(const std::string& block, std::string& title);

inline int metadataBlockLength(unsigned char lengthByte) { return lengthByte * 16; }

class TitleTracker {
 public:
  bool update(const std::string& block);

  const std::string& title() const { return title_; }
  void reset() { title_.clear(); }

 private:
  std::string title_;
};

}
}
