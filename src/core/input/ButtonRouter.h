#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "core/input/Buttons.h"

namespace awtrix {

class CoreEngine;

namespace input {

// The on-device menu as the buttons see it; the launcher implements it.
class IButtonMenu {
 public:
  virtual ~IButtonMenu() = default;
  virtual bool isOpen() const = 0;
  virtual void open(int64_t nowMs) = 0;
  virtual void close(int64_t nowMs) = 0;
  virtual void step(int direction, int64_t nowMs) = 0;
  virtual void confirm(int64_t nowMs) = 0;
};

// Turns the stable button state into actions, one update() per frame on every platform:
//   1. Holding select ends a running session, or closes the menu, or opens it.
//   2. While the menu is open it takes every new press: arrows step, releasing select confirms.
//   3. Otherwise scripts get first refusal, then the arrows navigate and select dismisses a
//      notification or, pressed twice, switches the panel on or off.
// A press the menu or a hold took stays theirs until it is released. Scripts see it end: pressed
// turns false while held is still true.
class ButtonRouter {
 public:
  using ScriptHook = std::function<bool(int button, bool pressed, bool held)>;

  static constexpr long kMenuHoldMs = 500;
  static constexpr long kDoublePressMs = 300;
  static constexpr long kStepRepeatDelayMs = 400;
  static constexpr long kStepRepeatMs = 150;

  void begin(CoreEngine& engine, IButtonMenu* menu) {
    engine_ = &engine;
    menu_ = menu;
  }
  void setScriptHook(ScriptHook hook) { hook_ = std::move(hook); }

  // physical is the debounced state by position; swapped mirrors left and right (a rotated
  // panel or the swap setting, both together cancel out).
  void update(const ButtonState& physical, bool swapped, int64_t nowMs);

 private:
  enum class Claim : uint8_t { None, Menu, Hold };

  void hold(const bool (&down)[kButtonCount], int64_t nowMs);
  void step(int button, int64_t nowMs, long nextInMs);

  CoreEngine* engine_ = nullptr;
  IButtonMenu* menu_ = nullptr;
  ScriptHook hook_;
  ButtonState physical_{};
  bool down_[kButtonCount] = {};
  Claim claim_[kButtonCount] = {};
  int64_t repeatAtMs_[kButtonCount] = {};
  int64_t selectDownMs_ = 0;
  int64_t lastSelectEdgeMs_ = -100000;
};

}

}
