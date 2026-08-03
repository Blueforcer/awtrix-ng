#include "core/render/RenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/CoreEngine.h"
#include "core/apps/SpecRenderer.h"
#include "core/render/StatusPixels.h"
#include "core/render/TransitionComposer.h"

namespace awtrix {

using render::drawLinkStatus;
using render::pulse;

namespace {
// Icons are 8 px wide; text starts one column further right.
constexpr int kIconWidth = 9;
const std::string kNoIcon;
constexpr long kDefaultTransMs = 1000;
constexpr long kIconRetryMs = 5000;
}

RenderPipeline::RenderPipeline(int width, int height, const RenderPipelineDeps& deps)
    : d_(deps), width_(width), height_(height) {
  slotA_.icon = d_.icons;
  slotB_.icon = d_.iconsB;
}

const AppSpec* RenderPipeline::pageSpec(const std::string& id, bool isNotif) const {
  if (isNotif) return &d_.engine->notifications().current();
  return d_.engine->pushedApp(id);
}

void RenderPipeline::loadIcon(PageSlot& slot, const std::string& pageId, const AppSpec* spec,
                              int64_t nowMs) {
  const std::string& wanted = spec ? spec->icon : kNoIcon;
  const bool same = slot.pageId == pageId && slot.iconId == wanted;
  if (!slot.icon || (same && (slot.valid || wanted.empty() || nowMs < slot.retryAtMs))) return;
  slot.pageId = pageId;
  slot.iconId = wanted;
  slot.valid = false;
  slot.icon->clear();
  if (!wanted.empty()) slot.valid = slot.icon->begin(wanted);
  slot.retryAtMs = nowMs + kIconRetryMs;
}

bool RenderPipeline::iconIsFullScreen(const PageSlot* slot, int canvasWidth) const {
  return slot && slot->valid && slot->icon && slot->icon->width() >= canvasWidth;
}

// How far left the icon is dragged by scrolling text. The icon rides along with the text until it
// has been pushed a full icon width off the left edge, then stays there.
int RenderPipeline::iconShift(const AppSpec& spec, const PageSlot& slot) const {
  if (spec.iconMode == IconMode::Fixed || spec.icon.empty()) return 0;
  if (slot.iconPushed && spec.iconMode == IconMode::PushOnce) return -kIconWidth;
  const float tx = slot.scroll.x();
  if (tx >= kIconWidth) return 0;
  const int shift = static_cast<int>(std::floor(tx)) - kIconWidth;
  return std::max(shift, -kIconWidth);
}

void RenderPipeline::renderPage(Canvas& dst, const std::string& id, int64_t nowMs, bool isNotif,
                                PageSlot* slot) {
  const Settings& s = d_.engine->state().settings();
  const RuntimeState& rt = d_.engine->state().runtime();

  auto drawOverlay = [&](const std::string& specName, const EffectSettings& specSettings) {
    const bool fromSpec = !specName.empty();
    IEffect* ov = d_.overlays->find(fromSpec ? specName : rt.globalOverlay);
    if (!ov) return;
    ov->setSettings(fromSpec ? specSettings : rt.globalOverlaySettings);
    ov->render(dst, ov->animationStep(nowMs));
  };

  auto drawSpec = [&](const AppSpec& spec) {
    // An icon as wide as the panel is treated as the background instead of a left-hand tile, so it
    // reserves no columns and the text draws straight on top of it.
    const bool fullScreen = iconIsFullScreen(slot, dst.width());
    render::SpecRender r;
    r.defaultTextColor = s.textColor;
    r.iconWidth = (spec.icon.empty() || !(slot && slot->valid) || fullScreen) ? 0 : kIconWidth;
    r.backgroundDrawn = fullScreen;
    r.nowMs = nowMs;
    r.textX = slot ? slot->scroll.x() : 0.0f;
    r.scroll = slot ? &slot->scroll.resolved() : nullptr;
    r.uppercase = s.uppercase;
    r.smoothScroll = s.smoothScroll;
    r.effect = d_.effects->find(spec.effect);
    EffectSettings es;
    es.speed = spec.extras().effectSpeed;
    es.hasSpeed = spec.extras().hasEffectSpeed;
    es.ramp = spec.extras().palette;
    if (r.effect) r.effect->setSettings(es);
    if (fullScreen) slot->icon->blit(dst, 0);
    render::renderSpec(dst, spec, fontFor(&spec), r);
    if (r.iconWidth && slot && slot->valid)
      slot->icon->blit(dst, spec.iconOffsetX + iconShift(spec, *slot));
    drawOverlay(spec.overlay, es);
  };

  if (isNotif) {
    drawSpec(d_.engine->notifications().current());
    return;
  }
  if (IApp* app = d_.apps->find(id)) {
    dst.clear(0x000000u);
    RenderCtx ctx;
    ctx.settings = &s;
    ctx.runtime = &rt;
    ctx.font = &fontFor(nullptr);
    ctx.fonts[0] = d_.fonts[0];
    ctx.fonts[1] = d_.fonts[1];
    d_.clock->fill(ctx, nowMs);
    app->render(dst, ctx);
    drawOverlay("", EffectSettings{});
  } else if (const AppSpec* cs = d_.engine->pushedApp(id)) {
    drawSpec(*cs);
  } else {
    dst.clear(0x000000u);
  }
}

const GfxFont& RenderPipeline::fontFor(const AppSpec* spec) const {
  const uint8_t i = static_cast<uint8_t>(spec ? spec->font : FontId::Small);
  const GfxFont* f = i < kFontCount ? d_.fonts[i] : nullptr;
  return *(f ? f : d_.fonts[0]);
}

render::ScrollLayout RenderPipeline::scrollLayoutFor(const AppSpec* spec, int canvasWidth,
                                                    bool iconReservesColumn) const {
  render::ScrollLayout layout;
  layout.canvasWidth = canvasWidth;
  layout.availWidth = canvasWidth;
  if (!spec) return layout;

  const Settings& s = d_.engine->state().settings();
  const bool hasIcon = !spec->icon.empty() && iconReservesColumn;
  layout.text = render::textMetricsFor(*spec, fontFor(spec), s.uppercase);
  layout.startX = hasIcon ? kIconWidth : 0;
  layout.availWidth = canvasWidth - (hasIcon ? kIconWidth : 0);
  layout.textOffset = spec->textOffsetX;
  return layout;
}

void RenderPipeline::applyScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs) {
  const bool iconReservesColumn = slot.valid && !iconIsFullScreen(&slot, width_);
  slot.scroll.set(spec ? spec->scroll : ScrollSpec{}, d_.engine->state().settings().scrollDefaults,
                  scrollLayoutFor(spec, width_, iconReservesColumn), nowMs);
}

void RenderPipeline::advanceScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs) {
  slot.scroll.advance(nowMs);
  if (spec && spec->iconMode == IconMode::PushOnce && !slot.iconPushed && slot.scroll.x() <= 0) {
    slot.iconPushed = true;
    slot.scroll.setStartX(0);
  }
}

void RenderPipeline::refreshPageContent(int64_t nowMs, bool isNotif) {
  const AppSpec* sp = pageSpec(lastRenderId_, isNotif);
  loadIcon(slotA_, lastRenderId_, sp, nowMs);
  applyScroll(slotA_, sp, nowMs);
}

void RenderPipeline::onPageChanged(int64_t nowMs, bool isNotif) {
  const AppSpec* sp = pageSpec(lastRenderId_, isNotif);
  const bool handover = slotB_.icon && slotB_.pageId == lastRenderId_;
  if (handover) std::swap(slotA_, slotB_);
  loadIcon(slotA_, lastRenderId_, sp, nowMs);
  applyScroll(slotA_, sp, nowMs);
  if (!handover) {
    slotA_.scroll.restart(nowMs);
    slotA_.iconPushed = false;
  }
  if (isNotif && d_.engine->state().settings().soundEnabled)
    d_.sound->play(d_.engine->notifications().current());
}

// The three status indicators are fixed pixel clusters on the right-hand edge: top corner, middle
// and bottom corner.
void RenderPipeline::drawIndicators(Canvas& out, int64_t nowMs) const {
  const RuntimeState& rt = d_.engine->state().runtime();
  const int right = out.width() - 1;
  const int bottom = out.height() - 1;
  const int mid = out.height() / 2;
  struct Shape {
    int count;
    int px[3][2];
  };
  const Shape shapes[3] = {
      {3, {{right, 0}, {right - 1, 0}, {right, 1}}},
      {2, {{right, mid - 1}, {right, mid}, {0, 0}}},
      {3, {{right, bottom}, {right, bottom - 1}, {right - 1, bottom}}},
  };
  for (int i = 0; i < 3; ++i) {
    const Indicator& ind = rt.indicators[i];
    if (!ind.on) continue;
    if (ind.blinkMs > 0 && ((nowMs / ind.blinkMs) & 1L)) continue;
    const uint32_t rgb = pulse(ind.color, nowMs, ind.fadeMs);
    for (int p = 0; p < shapes[i].count; ++p)
      out.setPixel(shapes[i].px[p][0], shapes[i].px[p][1], rgb);
  }
}

void RenderPipeline::renderFrame(Canvas& out, int64_t nowMs) {
  const Settings& s = d_.engine->state().settings();
  const bool isNotif = d_.engine->hasNotification();
  // Notifications get a synthetic page id: the \x01 prefix cannot collide with a real app name,
  // and folding in the generation makes every new notification look like a page change.
  const std::string renderId =
      isNotif ? "\x01notif:" + std::to_string(d_.engine->notifications().generation())
              : d_.engine->currentAppId();
  if (renderId != lastRenderId_) {
    lastRenderId_ = renderId;
    onPageChanged(nowMs, isNotif);
  } else {
    refreshPageContent(nowMs, isNotif);
  }

  if (isNotif && s.soundEnabled && !d_.sound->isPlaying()) {
    const AppSpec& n = d_.engine->notifications().current();
    if (n.loopSound) d_.sound->play(n);
  }

  const AppSpec* spec = pageSpec(renderId, isNotif);
  advanceScroll(slotA_, spec, nowMs);

  if (slotA_.icon) slotA_.icon->advance(nowMs);

  AppHost& ah = d_.engine->appHost();
  const bool inTransition =
      !isNotif && ah.inTransition() && ah.transitionTarget() >= 0 && ah.count() > 1;
  if (inTransition) {
    const std::string& toId = ah.idAt(ah.transitionTarget());
    const AppSpec* toSpec = d_.engine->pushedApp(toId);
    const bool entered = slotB_.pageId != toId;
    loadIcon(slotB_, toId, toSpec, nowMs);
    slotB_.pageId = toId;
    applyScroll(slotB_, toSpec, nowMs);
    if (entered) {
      slotB_.scroll.restart(nowMs);
      slotB_.iconPushed = false;
    }
    advanceScroll(slotB_, toSpec, nowMs);
    if (slotB_.icon) slotB_.icon->advance(nowMs);

    if (!transA_) {
      transA_.reset(new Canvas(width_, height_));
      transB_.reset(new Canvas(width_, height_));
    }
    renderPage(*transA_, ah.idAt(ah.currentIndex()), nowMs, false, &slotA_);
    renderPage(*transB_, toId, nowMs, false, &slotB_);
    const long perTrans = s.transitionDurationMs > 0 ? s.transitionDurationMs : kDefaultTransMs;
    const float p = static_cast<float>(nowMs - ah.phaseStartMs()) / perTrans;
    // Seeding on the phase start keeps a Random transition on one pick for its whole run.
    const Transition effect =
        render::resolveTransition(s.transitionEffect, static_cast<uint32_t>(ah.phaseStartMs()));
    render::composeTransition(out, *transA_, *transB_, effect, p, ah.direction());
  } else {
    transA_.reset();
    transB_.reset();
    slotB_.pageId.clear();
    renderPage(out, renderId, nowMs, isNotif, &slotA_);
  }

  drawIndicators(out, nowMs);
  {
    const RuntimeState& rt = d_.engine->state().runtime();
    drawLinkStatus(out, rt.wifi, rt.mqtt, nowMs);
  }

  const bool repeating = spec && slotA_.scroll.wantsMoreTime(spec->repeat);
  const bool passesDone = spec && slotA_.scroll.passesDone(spec->repeat);
  d_.engine->setRotationHold(!isNotif && repeating);
  d_.engine->setNotificationHold(isNotif && repeating);
  d_.engine->setNotificationPassesDone(d_.engine->notifications().generation(),
                                       isNotif && passesDone);
  d_.engine->setRotationPassesDone(renderId, !isNotif && passesDone);
}

}
