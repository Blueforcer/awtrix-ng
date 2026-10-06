#include "core/assets/AssetContent.h"

#include "core/render/PaletteFile.h"

namespace awtrix::assets {

const char* mimeType(std::string_view path) {
  const auto dot = path.rfind('.');
  const auto extension = dot == std::string_view::npos ? std::string_view() : path.substr(dot);
  if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
  if (extension == ".gif") return "image/gif";
  if (extension == ".png") return "image/png";
  if (extension == ".txt") return "text/plain";
  if (extension == ".mp3") return "audio/mpeg";
  return "application/octet-stream";
}

UploadValidator::UploadValidator(const std::string& path) { reset(path); }

void UploadValidator::reset(const std::string& path) {
  kind_ = kindFor(path);
  prefixSize_ = 0;
  invalid_ = false;
  nonempty_ = false;
  text_.clear();
}

bool UploadValidator::append(const uint8_t* data, std::size_t size) {
  if (invalid_) return false;
  if (size == 0) return true;
  if (!data || kind_ == AssetKind::Unknown) { invalid_ = true; return false; }
  nonempty_ = true;
  if (kind_ == AssetKind::Melody || kind_ == AssetKind::Palette) {
    const std::size_t limit =
        kind_ == AssetKind::Melody ? rtttl::kMaxLength : render::kMaxPaletteFileBytes;
    if (size > limit - text_.size()) { invalid_ = true; return false; }
    text_.append(reinterpret_cast<const char*>(data), size);
    return true;
  }
  for (std::size_t i = 0; i < size && prefixSize_ < sizeof(prefix_); ++i)
    prefix_[prefixSize_++] = data[i];
  return true;
}

bool UploadValidator::finish() const {
  if (invalid_ || !nonempty_) return false;
  switch (kind_) {
    case AssetKind::Melody: return rtttl::parse(text_).ok;
    case AssetKind::Palette: {
      render::Palette palette;
      return render::parsePaletteFile(text_, palette);
    }
    default: return contentLooksValid(kind_, prefix_, prefixSize_);
  }
}

}
