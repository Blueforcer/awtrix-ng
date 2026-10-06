#include "platform/linux/layout/LayoutPayload.h"

#include "core/api/JsonText.h"
#include "core/memory/CheckedShared.h"
#include "core/render/PageContent.h"
#include "platform/linux/layout/LayoutError.h"
#include "platform/linux/layout/LayoutJson.h"

namespace awtrix::layout {
namespace {
const char contentType = 0;
uint64_t nextRevision = 1;

class LayoutPage final : public PageContent {
 public:
  LayoutPage(LayoutSpec spec, DisplayProfile display, const Resources& resources,
             std::shared_ptr<Budget> budget)
      : parsed_(std::move(spec)), display_(display), resources_(resources), budget_(std::move(budget)) {}
  bool prepare(DispatchDetail& error) override {
    if (prepared_) return true;
    prepared_ = PreparedLayout::prepare(std::move(parsed_), display_, resources_, budget_, error);
    if (!prepared_) return false;
    parsed_ = LayoutSpec{};
    revision_ = nextRevision++;
    return true;
  }
  PageFrameResult draw(Canvas& canvas, const PageFrameContext& frame) override {
    if (!prepared_) return {};
    auto result = prepared_->draw(canvas, frame);
    result.revision = revision_;
    result.hasOverlay = prepared_->hasOverlay();
    return result;
  }
  void restart() override { if (prepared_) prepared_->restart(); }
  void invalidateAssets() override { if (prepared_) prepared_->invalidateAssets(); }
  void inheritState(PageContent& previous) override {
    if (previous.type() != type()) return;
    auto& before = static_cast<LayoutPage&>(previous);
    if (prepared_ && before.prepared_) prepared_->inheritState(*before.prepared_);
  }
  const void* type() const override { return &contentType; }
  bool repeats() const override {
    if (prepared_) return prepared_->repeats();
    for (const auto& region : parsed_.regions)
      if (region.kind == Kind::Text && region.repeat > 0) return true;
    return false;
  }
  uint64_t revision() const override { return revision_; }

 private:
  LayoutSpec parsed_;
  DisplayProfile display_;
  Resources resources_;
  std::shared_ptr<Budget> budget_;
  std::unique_ptr<PreparedLayout> prepared_;
  uint64_t revision_ = 0;
};
}

LayoutPayload::LayoutPayload(DisplayProfile display, const Resources& resources,
                              std::shared_ptr<Budget> budget)
    : display_(display), resources_(resources), budget_(std::move(budget)),
      handlers_{{"layout", this, validate, read}, {nullptr, nullptr, nullptr, nullptr}} {}

bool LayoutPayload::validate(api::JsonReader root, bool notification, DispatchDetail& error) {
  bool seen = false;
  if (!root.enterObject()) return false;
  while (root.nextMember()) {
    const auto key = root.key();
    const bool common = key == "layout" || key == "durationMs" || key == "repeat" ||
        key == "lifetimeMs" || key == "lifetimeExpiry";
    const bool notify = notification && (key == "name" || key == "hold" || key == "stack" ||
        key == "wakeup" || key == "sound");
    if ((!common && !notify) || (key == "layout" && seen)) {
      error = {std::string(key), "not allowed with layout"};
      return false;
    }
    if (key == "layout") seen = true;
    if (!root.skipValue()) return false;
  }
  return true;
}

bool LayoutPayload::read(void* context, api::JsonReader value, AppSpec& spec, DispatchDetail& error) {
  const auto& self = *static_cast<LayoutPayload*>(context);
  LayoutSpec parsed;
  if (!parse(value, parsed, error)) return false;
  auto content = checked::tryMakeShared<LayoutPage>(std::move(parsed), self.display_,
                                                   self.resources_, self.budget_);
  auto* extras = spec.tryExtrasMut();
  if (!content || !extras) return memoryFailure(error);
  extras->content = std::move(content);
  return true;
}

}
