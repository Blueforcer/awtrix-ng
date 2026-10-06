#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/render/PageInfo.h"

namespace awtrix {
namespace mirror {

// Which pages take part in mirroring. The app list is comma separated and matched without regard to
// case; "*" stands for every app and an empty list for none. A page sliding in counts as soon as
// its transition starts. External pages never pass, so a mirrored display is not passed on again.
class Filter {
 public:
  Filter() = default;
  Filter(const std::string& apps, bool notifications);

  bool admits(PageKind kind, std::string_view app, std::string_view incoming) const;
  bool admits(const PageInfo& page) const { return admits(page.kind, page.app, page.incoming); }

 private:
  bool listed(std::string_view app) const;

  bool allApps_ = true;
  std::vector<std::string> apps_;
  bool notifications_ = true;
};

}
}
