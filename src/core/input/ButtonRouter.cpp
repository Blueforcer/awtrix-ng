#include "core/input/ButtonRouter.h"

#include "core/Command.h"
#include "core/CoreEngine.h"

namespace awtrix::input {

namespace {
constexpr int kLeft = index(Button::Left);
constexpr int kSelect = index(Button::Select);
constexpr int kRight = index(Button::Right);
}

void ButtonRouter::update(const ButtonState& physical, bool swapped, int64_t nowMs) {
  if (!engine_) return;
  const bool down[kButtonCount] = {swapped ? physical.right : physical.left, physical.select,
                                   swapped ? physical.left : physical.right};
  bool edge[kButtonCount];
  for (int i = 0; i < kButtonCount; ++i) edge[i] = down[i] && !down_[i];
  const bool blocked = engine_->state().settings().blockNavigation;
  if (edge[kSelect]) selectDownMs_ = nowMs;

  if (down[kSelect] && claim_[kSelect] != Claim::Hold && nowMs - selectDownMs_ >= kMenuHoldMs)
    hold(down, nowMs);

  if (menu_ && menu_->isOpen()) {
    for (int i = 0; i < kButtonCount; ++i) {
      if (!edge[i] || claim_[i] != Claim::None) continue;
      claim_[i] = Claim::Menu;
      if (i != kSelect) step(i, nowMs, kStepRepeatDelayMs);
    }
    for (const int i : {kLeft, kRight})
      if (down[i] && claim_[i] == Claim::Menu && nowMs >= repeatAtMs_[i])
        step(i, nowMs, kStepRepeatMs);
  }
  if (!down[kSelect] && down_[kSelect] && claim_[kSelect] == Claim::Menu && menu_ &&
      menu_->isOpen())
    menu_->confirm(nowMs);

  bool took[kButtonCount] = {};
  if (hook_)
    for (int i = 0; i < kButtonCount; ++i) took[i] = hook_(i, down[i] && claim_[i] == Claim::None, down[i]);

  const auto free = [&](int i) { return edge[i] && claim_[i] == Claim::None && !took[i]; };
  const bool navigate = !blocked && !engine_->inSession();
  if (free(kLeft) && navigate) engine_->submit(Command(CommandType::PreviousApp));
  if (free(kRight) && navigate) engine_->submit(Command(CommandType::NextApp));
  // A taken press is not remembered, so it can never pair up with the next one into a double
  // press.
  if (free(kSelect)) {
    engine_->submit(Command(CommandType::DismissNotify));
    if (!blocked && nowMs - lastSelectEdgeMs_ <= kDoublePressMs) {
      Command c(CommandType::SetDisplay);
      c.payload = engine_->state().runtime().matrixOff ? "{\"power\":true}" : "{\"power\":false}";
      // Applied at once, so a following edge or API command reads the new power state.
      engine_->execute(c);
    }
    lastSelectEdgeMs_ = nowMs;
  }

  for (int i = 0; i < kButtonCount; ++i) {
    if (!down[i]) claim_[i] = Claim::None;
    down_[i] = down[i];
  }
  if (physical.left != physical_.left || physical.select != physical_.select ||
      physical.right != physical_.right) {
    engine_->state().runtime().buttons = {physical.left, physical.select, physical.right};
    engine_->state().emit(StateEvent::ButtonsChanged);
  }
  physical_ = physical;
}

// Only a hold that did something takes the buttons held with it.
void ButtonRouter::hold(const bool (&down)[kButtonCount], int64_t nowMs) {
  if (engine_->inSession()) {
    engine_->endSession();
  } else if (menu_ && menu_->isOpen()) {
    menu_->close(nowMs);
  } else if (menu_ && !engine_->state().settings().blockNavigation &&
             !engine_->state().runtime().matrixOff) {
    menu_->open(nowMs);
  } else {
    return;
  }
  for (int i = 0; i < kButtonCount; ++i)
    if (down[i]) claim_[i] = Claim::Hold;
}

// Holding an arrow keeps stepping, after a first pause.
void ButtonRouter::step(int button, int64_t nowMs, long nextInMs) {
  repeatAtMs_[button] = nowMs + nextInMs;
  menu_->step(button == kLeft ? -1 : 1, nowMs);
}

}
