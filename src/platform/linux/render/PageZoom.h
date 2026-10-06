#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "core/payload/AppSpec.h"
#include "core/render/PageIcon.h"
#include "core/render/PageZoom.h"

namespace awtrix {

// With enlargeApps on, a page made for eight rows fills a taller panel: one page pixel becomes a
// square of panel pixels, as many rows high as the panel has rows of eight. A page with an image
// too big for the stage keeps its size.
class PageZoom final : public IPageZoom {
 public:
  PageZoom(int width, int height, const IPageIcon& images);

  bool available() const { return stage_.valid(); }
  bool enlarges(const Settings& settings, const AppSpec& page, uint32_t assets) override;
  Canvas& stage() override { return stage_; }
  void present(Canvas& frame) const override;

 private:
  // The images of one page and whether they all fit the stage. Two are kept: the page on screen
  // and the one sliding in.
  struct Judgment {
    bool valid = false;
    uint32_t assets = 0;
    std::string icon;
    std::array<std::string, kMaxPlacedIcons> placed;
    std::size_t placedCount = 0;
    bool fits = false;
  };

  bool fits(const std::string& image) const;
  static bool judges(const Judgment& judgment, const AppSpec& page, uint32_t assets);

  const IPageIcon& images_;
  int width_, height_;
  int factor_;
  Canvas stage_;
  std::array<Judgment, 2> judged_;
  std::size_t next_ = 0;
};

}
