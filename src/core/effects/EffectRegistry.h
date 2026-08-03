#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/StrCase.h"
#include "core/effects/IEffect.h"

namespace awtrix {

class EffectRegistry {
 public:
  void add(IEffect* effect) {
    if (!effect) return;
    for (IEffect*& e : effects_)
      if (strcase::equalsIgnoreCase(e->id(), effect->id())) {
        e = effect;
        return;
      }
    effects_.push_back(effect);
  }
  IEffect* find(const std::string& id) const {
    if (id.empty()) return nullptr;
    for (IEffect* e : effects_)
      if (strcase::equalsIgnoreCase(e->id(), id)) return e;
    return nullptr;
  }
  std::size_t size() const { return effects_.size(); }
  std::vector<std::string> names() const {
    std::vector<std::string> v;
    v.reserve(effects_.size());
    for (const IEffect* e : effects_) v.push_back(e->id());
    sortLikeTheOldMap(v);
    return v;
  }
  std::vector<std::string> paletteNames() const {
    std::vector<std::string> v;
    for (const IEffect* e : effects_)
      if (e->usesPalette()) v.push_back(e->id());
    sortLikeTheOldMap(v);
    return v;
  }

 private:
  // Reproduces the case-insensitive ordering of the std::map this registry replaced, so the API
  // still lists effects in the order clients have always seen.
  static void sortLikeTheOldMap(std::vector<std::string>& v) {
    std::sort(v.begin(), v.end(), [](const std::string& a, const std::string& b) {
      const std::size_t n = a.size() < b.size() ? a.size() : b.size();
      for (std::size_t i = 0; i < n; ++i) {
        const char ca = strcase::lower(a[i]), cb = strcase::lower(b[i]);
        if (ca != cb) return ca < cb;
      }
      return a.size() < b.size();
    });
  }

  std::vector<IEffect*> effects_;
};

}
