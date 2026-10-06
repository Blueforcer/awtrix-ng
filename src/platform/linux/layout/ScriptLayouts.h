#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "platform/linux/layout/Layout.h"

namespace awtrix::script {

// The host owns handles; identity includes the script, and IDs are never reused after release.
class ScriptLayouts {
 public:
  static constexpr std::size_t kMaxHandles = 8;
  static constexpr std::size_t kMaxPerScript = 4;

  ScriptLayouts(DisplayProfile display, const layout::Resources& resources,
                std::shared_ptr<layout::Budget> budget)
      : display_(display), resources_(resources), budget_(std::move(budget)) {}
  int32_t prepare(const std::string& owner, std::string_view json,
                  std::string& error);
  bool update(const std::string& owner, int32_t handle, std::string_view json,
              std::string& error);
  bool draw(const std::string& owner, int32_t handle, Canvas& canvas,
            const layout::FrameContext& frame, bool& finished, std::string& error);
  bool release(const std::string& owner, int32_t handle);
  void purge(const std::string& owner);
  void beginFrame(const std::string& owner);
  void restart(const std::string& owner);
  void invalidateAssets();
  bool wantsMoreTime(const std::string& owner) const;
  std::size_t size() const;

 private:
  struct Entry {
    checked::CheckedString owner;
    int32_t id = 0;
    std::unique_ptr<layout::PreparedLayout> layout;
    bool drawn = false;
    bool wantsMoreTime = false;
  };
  Entry* find(const std::string& owner, int32_t id);
  std::array<Entry, kMaxHandles> entries_;
  int32_t nextId_ = 1;
  DisplayProfile display_;
  layout::Resources resources_;
  std::shared_ptr<layout::Budget> budget_;
};

}
