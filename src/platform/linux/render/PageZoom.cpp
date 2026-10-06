#include "platform/linux/render/PageZoom.h"

#include <algorithm>

#include "core/Settings.h"

namespace awtrix {

namespace {
constexpr int kPageRows = 8;

std::size_t placedCount(const AppSpec& page) {
  return std::min(page.extras().icons.size(), kMaxPlacedIcons);
}
}

PageZoom::PageZoom(int width, int height, const IPageIcon& images)
    : images_(images), width_(width), height_(height), factor_(height / kPageRows),
      stage_(factor_ > 1 ? width / factor_ : 0, factor_ > 1 ? height / factor_ : 0) {}

bool PageZoom::enlarges(const Settings& settings, const AppSpec& page, uint32_t assets) {
  if (!settings.enlargeApps || !available()) return false;
  for (const Judgment& judgment : judged_)
    if (judges(judgment, page, assets)) return judgment.fits;
  Judgment& judgment = judged_[next_];
  next_ = (next_ + 1) % judged_.size();
  judgment.valid = true;
  judgment.assets = assets;
  judgment.icon = page.icon;
  judgment.placedCount = placedCount(page);
  judgment.fits = fits(page.icon);
  for (std::size_t i = 0; i < judgment.placedCount; ++i) {
    judgment.placed[i] = page.extras().icons[i].icon;
    judgment.fits = judgment.fits && fits(judgment.placed[i]);
  }
  return judgment.fits;
}

bool PageZoom::judges(const Judgment& judgment, const AppSpec& page, uint32_t assets) {
  if (!judgment.valid || judgment.assets != assets || judgment.icon != page.icon ||
      judgment.placedCount != placedCount(page))
    return false;
  for (std::size_t i = 0; i < judgment.placedCount; ++i)
    if (judgment.placed[i] != page.extras().icons[i].icon) return false;
  return true;
}

// Only an image known to be bigger than the stage counts against it; built-in, missing and remote
// pictures fit, a remote one is fitted to the box it is given.
bool PageZoom::fits(const std::string& image) const {
  if (image.empty()) return true;
  int width = 0, height = 0;
  std::size_t bytes = 0;
  return images_.nativeInfo(image, stage_.width(), stage_.height(), width, height, bytes) ||
         !images_.nativeInfo(image, width_, height_, width, height, bytes);
}

void PageZoom::present(Canvas& frame) const {
  frame.clear();
  const int columns = std::min(stage_.width(), frame.width() / factor_);
  const int rows = std::min(stage_.height(), frame.height() / factor_);
  const std::size_t stride = static_cast<std::size_t>(frame.width());
  for (int y = 0; y < rows; ++y) {
    const uint32_t* source = stage_.data() + static_cast<std::size_t>(y) * stage_.width();
    uint32_t* first = frame.data() + static_cast<std::size_t>(y) * factor_ * stride;
    for (int x = 0; x < columns; ++x) std::fill_n(first + x * factor_, factor_, source[x]);
    for (int copy = 1; copy < factor_; ++copy) std::copy_n(first, stride, first + copy * stride);
  }
}

}
