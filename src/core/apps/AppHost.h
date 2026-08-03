#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace awtrix {

enum class AppPhase : uint8_t { Fixed, InTransition };

// Rotation state machine over app ids: which one is showing, and the transition to the next.
// It holds ids only, never the apps themselves.
class AppHost {
 public:
  // Returns false for apps that must be skipped right now, for example disabled or without data.
  using ShowGate = std::function<bool(const std::string& id)>;

  void setShowGate(ShowGate gate) { gate_ = std::move(gate); }

  void setApps(const std::vector<std::string>& ids);

  std::size_t count() const { return apps_.size(); }
  bool empty() const { return apps_.empty(); }
  const std::vector<std::string>& ids() const { return apps_; }
  int currentIndex() const { return current_; }
  const std::string& currentId() const;
  AppPhase phase() const { return phase_; }
  bool inTransition() const { return phase_ == AppPhase::InTransition; }
  int transitionTarget() const { return target_; }
  int64_t phaseStartMs() const { return phaseStartMs_; }
  const std::string& idAt(int index) const {
    return (index >= 0 && index < static_cast<int>(apps_.size())) ? apps_[index] : currentId();
  }

  void tick(int64_t nowMs, long appDurationMs, long transitionDurationMs, bool autoTransition);

  void next(int64_t nowMs);
  void previous(int64_t nowMs);

  bool switchTo(const std::string& id, int64_t nowMs);
  bool transitionTo(const std::string& id, int64_t nowMs);
  int direction() const { return direction_; }

 private:
  void beginTransition(int dir, int64_t nowMs);
  void settle(int64_t nowMs);
  void startPhase(AppPhase phase, int64_t nowMs);
  int pick(int from, int dir) const;
  int indexOf(const std::string& id) const;
  ShowGate gate_;
  std::vector<std::string> apps_;
  int current_ = 0;
  int target_ = -1;
  int direction_ = 1;
  AppPhase phase_ = AppPhase::Fixed;
  int64_t phaseStartMs_ = 0;
  bool clockStarted_ = false;
};

}
