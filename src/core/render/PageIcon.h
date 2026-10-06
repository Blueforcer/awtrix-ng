#pragma once

#include <memory>
#include <string>
#include <string_view>
#include "core/render/Canvas.h"

namespace awtrix {

enum class IconLoad : uint8_t { kGood, kMissing, kOom };

class IPageIcon {
 public:
  virtual ~IPageIcon() = default;
  virtual IconLoad begin(const std::string& iconId, int maxWidth, int maxHeight) = 0;
  virtual IconLoad beginNative(std::string_view iconId, int maxWidth, int maxHeight) {
    (void)iconId; (void)maxWidth; (void)maxHeight; return IconLoad::kMissing;
  }
  virtual IconLoad beginNativeBounded(std::string_view iconId, int maxWidth, int maxHeight,
                                     std::size_t residentBytes) {
    (void)residentBytes; return beginNative(iconId, maxWidth, maxHeight);
  }
  virtual void clear() = 0;
  virtual void advance(int64_t nowMs) = 0;
  virtual void blit(Canvas& dst, int xOffset, int yOffset = 0) const = 0;
  virtual int width() const = 0;
  virtual int height() const { return 8; }
  virtual bool nativeInfo(std::string_view, int, int, int&, int&, std::size_t&) const {
    return false;
  }
  // Allocated only for the additional absolute-position icons requested by a page.
  virtual std::unique_ptr<IPageIcon> create() const { return nullptr; }
};

}
