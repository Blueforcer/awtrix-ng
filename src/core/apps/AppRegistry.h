#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/apps/IApp.h"

namespace awtrix {

// Non-owning lookup table: it stores bare pointers, so every registered app has to outlive it.
// Adding an id that is already there replaces the entry instead of duplicating it.
class AppRegistry {
 public:
  void add(IApp* app) {
    if (!app) return;
    for (IApp*& a : apps_)
      if (a->id() == app->id()) {
        a = app;
        return;
      }
    apps_.push_back(app);
  }
  IApp* find(const std::string& id) const {
    for (IApp* a : apps_)
      if (a->id() == id) return a;
    return nullptr;
  }
  void remove(const std::string& id) {
    apps_.erase(std::remove_if(apps_.begin(), apps_.end(),
                               [&](IApp* a) { return a->id() == id; }),
                apps_.end());
  }
  std::size_t size() const { return apps_.size(); }

 private:
  std::vector<IApp*> apps_;
};

}
