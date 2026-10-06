#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/AssetPaths.h"

namespace awtrix::assets {

// Independent of transport chunk boundaries: media keep their first four bytes, melodies and
// palettes their whole text, at most 512 bytes each.
// Both multipart uploads and streaming backup restores use the same content policy.
class UploadValidator {
 public:
  explicit UploadValidator(const std::string& path = {});
  void reset(const std::string& path);
  bool append(const uint8_t* data, std::size_t size);
  bool finish() const;

 private:
  AssetKind kind_ = AssetKind::Unknown;
  unsigned char prefix_[4]{};
  unsigned prefixSize_ = 0;
  bool invalid_ = false;
  bool nonempty_ = false;
  std::string text_;
};

}
