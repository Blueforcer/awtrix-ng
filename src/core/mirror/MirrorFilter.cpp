#include "core/mirror/MirrorFilter.h"

#include <algorithm>

#include "core/StrCase.h"

namespace awtrix {
namespace mirror {

namespace {
bool blank(char c) { return c == ' ' || c == '\t'; }

bool sameName(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return strcase::lower(x) == strcase::lower(y); });
}
}

Filter::Filter(const std::string& apps, bool notifications)
    : allApps_(false), notifications_(notifications) {
  std::size_t start = 0;
  while (start <= apps.size()) {
    std::size_t end = apps.find(',', start);
    if (end == std::string::npos) end = apps.size();
    std::size_t first = start, last = end;
    while (first < last && blank(apps[first])) ++first;
    while (last > first && blank(apps[last - 1])) --last;
    if (last > first) {
      std::string name = apps.substr(first, last - first);
      if (name == "*") allApps_ = true;
      else apps_.push_back(std::move(name));
    }
    start = end + 1;
  }
}

bool Filter::listed(std::string_view app) const {
  if (app.empty()) return false;
  if (allApps_) return true;
  return std::any_of(apps_.begin(), apps_.end(),
                     [app](const std::string& name) { return sameName(name, app); });
}

bool Filter::admits(PageKind kind, std::string_view app, std::string_view incoming) const {
  switch (kind) {
    case PageKind::Notification: return notifications_;
    case PageKind::External: return false;
    case PageKind::App: break;
  }
  return listed(app) || listed(incoming);
}

}
}
