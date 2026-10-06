#include "core/render/RenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <string>

#include "core/CoreEngine.h"
#include "core/apps/SpecRenderer.h"
#include "core/render/StatusPixels.h"
#include "core/render/TransitionComposer.h"

namespace awtrix {

using render::drawLinkStatus;
using render::pulse;

namespace {
const std::string kNoIcon;
constexpr long kDefaultTransMs = 1000;
constexpr long kIconRetryMs = 5000;

}

RenderPipeline::RenderPipeline(int width, int height, const RenderPipelineDeps& deps)
    : d_(deps), width_(width), height_(height) {
  slotA_.icon = d_.icons;
  slotB_.icon = d_.iconsB;
  if (d_.engine) d_.engine->setFontCatalog(d_.fonts);
}

bool RenderPipeline::prepareFrames() {
  if (!transA_) transA_.reset(new (std::nothrow) Canvas(width_, height_));
  if (!transB_) transB_.reset(new (std::nothrow) Canvas(width_, height_));
  return transA_ && transB_ && transA_->valid() && transB_->valid();
}

void RenderPipeline::invalidateIcons() {
  ++iconGeneration_;
  if (d_.engine) d_.engine->invalidateContentAssets();
}

const AppSpec* RenderPipeline::pageSpec(const std::string& id, PageKind kind) const {
  switch (kind) {
    case PageKind::Notification: return &d_.engine->notifications().current();
    case PageKind::External: return nullptr;
    case PageKind::App: break;
  }
  if (d_.engine->isScriptApp(id)) return nullptr;
  return d_.engine->pushedApp(id);
}

PageKind RenderPipeline::pageKind() const {
  if (d_.engine->hasNotification()) return PageKind::Notification;
  if (d_.external && d_.external->active()) return PageKind::External;
  return PageKind::App;
}

// Synthetic ids for notifications and the external page: a control-character prefix
// followed by the generation.
std::string RenderPipeline::pageId(PageKind kind) const {
  switch (kind) {
    case PageKind::Notification:
      return "\x01notif:" + std::to_string(d_.engine->notifications().generation());
    case PageKind::External: return "\x02" "external";
    case PageKind::App: break;
  }
  return d_.engine->currentAppId();
}

void RenderPipeline::loadIcon(PageSlot& slot, const std::string& pageId, const AppSpec* spec,
                              int64_t nowMs) {
  const std::string& wanted = spec ? spec->icon : kNoIcon;
  const uint32_t generation = iconGeneration_.load();
  const bool enlarged = enlargeFor(spec, generation);
  const bool reloadAll =
      slot.pageId != pageId || slot.iconGeneration != generation || slot.enlarged != enlarged;
  if (reloadAll) {
    // Release the outgoing page before opening any incoming image, so its resident pixels
    // cannot force an otherwise unnecessary allocation failure and five-second retry.
    slot.placedIcons.reset();
    slot.placedIconCount = 0;
    slot.placedRetryAtMs = 0;
  }
  const bool same = !reloadAll && slot.iconId == wanted;
  slot.pageId = pageId;
  slot.iconGeneration = generation;
  slot.enlarged = enlarged;
  if (slot.icon &&
      !(same && (slot.valid || slot.missing || wanted.empty() || nowMs < slot.retryAtMs))) {
    slot.iconId = wanted;
    slot.valid = slot.missing = false;
    slot.icon->clear();
    if (!wanted.empty()) {
      const IconLoad result = slot.icon->begin(wanted, pageWidth(slot), pageHeight(slot));
      slot.valid = result == IconLoad::kGood;
      slot.missing = result == IconLoad::kMissing;
      iconLoadedThisFrame_ = true;
    }
    slot.retryAtMs = nowMs + kIconRetryMs;
  }
  loadPlacedIcons(slot, spec, nowMs);
}

void RenderPipeline::loadPlacedIcons(PageSlot& slot, const AppSpec* spec, int64_t nowMs) {
  const std::size_t count = spec ? std::min(spec->extras().icons.size(), kMaxPlacedIcons) : 0;
  slot.iconsPending = false;
  if (count != slot.placedIconCount) {
    slot.placedIcons.reset();
    slot.placedIconCount = 0;
  }
  if (count == 0 || !slot.icon) return;
  if (!slot.placedIcons) {
    if (nowMs < slot.placedRetryAtMs) return;
    slot.placedIcons.reset(new (std::nothrow) PlacedIcon[count]);
    if (!slot.placedIcons) {
      slot.placedRetryAtMs = nowMs + kIconRetryMs;
      return;
    }
    slot.placedIconCount = count;
  }
  for (std::size_t i = 0; i < count; ++i) {
    auto& icon = slot.placedIcons[i];
    const auto& wanted = spec->extras().icons[i];
    icon.x = wanted.x;
    icon.y = wanted.y;
    if (icon.iconId != wanted.icon) {
      icon.iconId = wanted.icon;
      icon.valid = icon.missing = false;
      icon.retryAtMs = 0;
      if (icon.player) icon.player->clear();
    }
    if (icon.valid || icon.missing || nowMs < icon.retryAtMs) continue;
    if (iconLoadedThisFrame_) {
      slot.iconsPending = true;
      continue;
    }
    if (!icon.player) icon.player = slot.icon->create();
    if (icon.player) {
      const IconLoad result = icon.player->begin(icon.iconId, pageWidth(slot), pageHeight(slot));
      icon.valid = result == IconLoad::kGood;
      icon.missing = result == IconLoad::kMissing;
      iconLoadedThisFrame_ = true;
    }
    icon.retryAtMs = nowMs + kIconRetryMs;
  }
}

void RenderPipeline::advanceIcons(PageSlot& slot, int64_t nowMs) {
  if (slot.iconsPending) return;
  if (slot.icon) slot.icon->advance(nowMs);
  for (std::size_t i = 0; i < slot.placedIconCount; ++i) {
    auto& icon = slot.placedIcons[i];
    if (icon.valid) icon.player->advance(nowMs);
  }
}

bool RenderPipeline::iconIsFullScreen(const PageSlot* slot, int canvasWidth) const {
  return slot && slot->valid && slot->icon && slot->icon->width() >= canvasWidth;
}

// The columns an icon keeps free of text: its own width plus the page's gap. A missing or
// full-screen icon keeps none.
int RenderPipeline::iconColumn(const AppSpec& spec, const PageSlot* slot) const {
  if (spec.icon.empty() || !slot || !slot->valid || !slot->icon ||
      iconIsFullScreen(slot, pageWidth(*slot)))
    return 0;
  return std::min(slot->icon->width() + spec.iconGap, pageWidth(*slot));
}

bool RenderPipeline::enlargeFor(const AppSpec* spec, uint32_t assets) const {
  return d_.zoom && spec && d_.zoom->enlarges(d_.engine->state().settings(), *spec, assets);
}

// How far left the icon is dragged by scrolling text. The icon rides along with the text until it
// and its gap have been pushed off the left edge, then stays there.
int RenderPipeline::iconShift(const AppSpec& spec, const PageSlot& slot) const {
  if (spec.iconMode == IconMode::Fixed) return 0;
  const int column = iconColumn(spec, &slot);
  if (column == 0) return 0;
  if (slot.iconPushed && spec.iconMode == IconMode::PushOnce) return -column;
  const float tx = slot.scroll.x();
  if (tx >= column) return 0;
  const int shift = static_cast<int>(std::floor(tx)) - column;
  return std::max(shift, -column);
}

void RenderPipeline::renderPage(Canvas& dst, const std::string& id, int64_t nowMs, PageKind kind,
                                PageSlot* slot) {
  if (kind == PageKind::External) {
    dst.clear(0x000000u);
    d_.external->draw(dst);
    return;
  }
  const bool isNotif = kind == PageKind::Notification;
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
    if (spec.extras().content) {
      auto& native = *spec.extras().content;
      PageFrameContext frame;
      frame.nowMs = nowMs;
      frame.defaultColor = s.textColor;
      frame.repeat = spec.repeat;
      frame.scrollDefaults = s.scrollDefaults;
      frame.parkAfterPasses = d_.engine->endsOnScrollPasses(spec, isNotif);
      frame.uppercase = s.uppercase;
      const auto result = native.draw(dst, frame);
      if (slot) slot->contentFrame = result;
      if (spec.lifeTimeEnd) dst.drawRect(0, 0, dst.width(), dst.height(), 0x6e0700u);
      if (!result.hasOverlay) drawOverlay("", EffectSettings{});
      return;
    }
    const bool enlarged = slot && slot->enlarged;
    Canvas& page = enlarged ? d_.zoom->stage() : dst;
    // An icon as wide as the panel is treated as the background instead of a left-hand tile, so it
    // reserves no columns and the text draws straight on top of it.
    const bool fullScreen = iconIsFullScreen(slot, page.width());
    const int column = iconColumn(spec, slot);
    render::SpecRender r;
    r.defaultTextColor = s.textColor;
    r.iconWidth = column ? slot->icon->width() : 0;
    r.iconGap = column - r.iconWidth;
    r.textClipLeft = column ? column + iconShift(spec, *slot) : 0;
    const FontEntry& font = fontFor(&spec);
    r.baseline = pageBaseline(font, page.height());
    r.drawBaseline = pageBaseline(font);
    r.backgroundDrawn = fullScreen;
    r.nowMs = nowMs;
    r.textX = slot ? slot->scroll.x() : 0.0f;
    r.scroll = slot ? &slot->scroll.resolved() : nullptr;
    r.uppercase = s.uppercase;
    r.effect = d_.effects->find(spec.effect);
    EffectSettings es;
    es.speed = spec.extras().effectSpeed;
    es.hasSpeed = spec.extras().hasEffectSpeed;
    es.ramp = spec.extras().palette;
    if (r.effect) r.effect->setSettings(es);
    const int iconY = slot && slot->icon ? std::max(0, (page.height() - slot->icon->height()) / 2) : 0;
    if (fullScreen) slot->icon->blit(page, 0, iconY);
    render::renderSpec(page, spec, *font.font, r);
    if (r.iconWidth && slot && slot->valid)
      slot->icon->blit(page, spec.iconOffsetX + iconShift(spec, *slot), iconY);
    if (slot) {
      for (std::size_t i = 0; i < slot->placedIconCount; ++i) {
        const auto& icon = slot->placedIcons[i];
        if (icon.valid) icon.player->blit(page, icon.x, icon.y);
      }
    }
    if (enlarged) d_.zoom->present(dst);
    drawOverlay(spec.overlay, es);
  };

  if (isNotif) {
    drawSpec(d_.engine->notifications().current());
    return;
  }
  const AppSpec* pushed = pageSpec(id, PageKind::App);
  IApp* app = pushed ? nullptr : d_.apps->find(id);
  if (app) {
    dst.clear(0x000000u);
    RenderCtx ctx;
    ctx.settings = &s;
    ctx.runtime = &rt;
    ctx.font = d_.fonts->small().font;
    ctx.fonts = d_.fonts;
    d_.clock->fill(ctx, nowMs);
    ctx.shownSinceMs = slot == &slotB_ ? -1 : shownSinceMs_;
    app->render(dst, ctx);
    drawOverlay("", EffectSettings{});
  } else if (pushed) {
    drawSpec(*pushed);
  } else {
    dst.clear(0x000000u);
  }
}

const FontEntry& RenderPipeline::fontFor(const AppSpec* spec) const {
  const FontEntry* named = spec && !spec->font.empty() ? d_.fonts->find(spec->font) : nullptr;
  return named ? *named : d_.fonts->small();
}

render::ScrollLayout RenderPipeline::scrollLayoutFor(const AppSpec* spec, int canvasWidth,
                                                    int column) const {
  render::ScrollLayout layout;
  layout.canvasWidth = canvasWidth;
  layout.availWidth = canvasWidth;
  if (!spec) return layout;

  const Settings& s = d_.engine->state().settings();
  layout.text = render::textMetricsFor(*spec, *fontFor(spec).font, s.uppercase);
  layout.startX = column;
  layout.availWidth = canvasWidth - column;
  layout.textOffset = spec->textOffsetX;
  return layout;
}

void RenderPipeline::applyScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs) {
  slot.scroll.set(spec ? spec->scroll : ScrollSpec{}, d_.engine->state().settings().scrollDefaults,
                  scrollLayoutFor(spec, pageWidth(slot), spec ? iconColumn(*spec, &slot) : 0), nowMs);
}

int RenderPipeline::scrollParkAfter(const AppSpec* spec, PageKind kind) const {
  return spec && d_.engine->endsOnScrollPasses(*spec, kind == PageKind::Notification) ? spec->repeat
                                                                                      : 0;
}

void RenderPipeline::advanceScroll(PageSlot& slot, const AppSpec* spec, int64_t nowMs,
                                   int parkAfter) {
  slot.scroll.advance(nowMs, parkAfter);
  if (spec && spec->iconMode == IconMode::PushOnce && !slot.iconPushed && slot.scroll.x() <= 0) {
    slot.iconPushed = true;
    slot.scroll.setStartX(0);
  }
}

void RenderPipeline::refreshPageContent(int64_t nowMs, PageKind kind) {
  const AppSpec* sp = pageSpec(lastRenderId_, kind);
  loadIcon(slotA_, lastRenderId_, sp, nowMs);
  applyScroll(slotA_, sp, nowMs);
}

void RenderPipeline::onPageChanged(int64_t nowMs, PageKind kind) {
  const AppSpec* sp = pageSpec(lastRenderId_, kind);
  const bool handover = slotB_.icon && slotB_.pageId == lastRenderId_;
  if (handover) std::swap(slotA_, slotB_);
  loadIcon(slotA_, lastRenderId_, sp, nowMs);
  applyScroll(slotA_, sp, nowMs);
  if (!handover) {
    slotA_.scroll.restart(nowMs);
    slotA_.iconPushed = false;
    if (sp && sp->extras().content) sp->extras().content->restart();
  }
  // A looping sound belongs to the notification that started it; one without loop plays out.
  d_.audio->stopRepeatingAlert(alertRepeat_);
  alertRepeat_ = 0;
  if (kind == PageKind::Notification) playPageSound(d_.engine->notifications().current());
}

void RenderPipeline::playPageSound(const AppSpec& spec) {
  if (spec.sound.empty()) return;
  sound::Choices choices;
  DispatchDetail detail;
  if (!sound::parse(spec.sound, sound::Origin::Notification, choices, detail)) return;
  if (d_.audio->play(choices, sound::Group::Alert, spec.extras().soundScript, detail) ==
      sound::PlayResult::Ok)
    alertRepeat_ = d_.audio->repeatingAlert();
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
  iconLoadedThisFrame_ = false;
  const Settings& s = d_.engine->state().settings();
  const PageKind kind = pageKind();
  const bool isNotif = kind == PageKind::Notification;
  const bool isApp = kind == PageKind::App;
  const std::string renderId = pageId(kind);
  if (renderId != lastRenderId_) {
    lastRenderId_ = renderId;
    onPageChanged(nowMs, kind);
    shownSinceMs_ = nowMs;
  } else {
    refreshPageContent(nowMs, kind);
    if (skipped_) shownSinceMs_ = nowMs;
  }
  skipped_ = false;

  const AppSpec* spec = pageSpec(renderId, kind);
  advanceScroll(slotA_, spec, nowMs, scrollParkAfter(spec, kind));

  advanceIcons(slotA_, nowMs);

  AppHost& ah = d_.engine->appHost();
  const bool inTransition =
      isApp && ah.inTransition() && ah.transitionTarget() >= 0 && ah.count() > 1;
  shown_.kind = kind;
  if (isApp) shown_.app = renderId;
  else shown_.app.clear();
  if (inTransition) shown_.incoming = ah.idAt(ah.transitionTarget());
  else shown_.incoming.clear();
  if (inTransition) {
    const std::string& toId = ah.idAt(ah.transitionTarget());
    const AppSpec* toSpec = pageSpec(toId, PageKind::App);
    const bool entered = slotB_.pageId != toId;
    loadIcon(slotB_, toId, toSpec, nowMs);
    slotB_.pageId = toId;
    applyScroll(slotB_, toSpec, nowMs);
    if (entered) {
      slotB_.scroll.restart(nowMs);
      slotB_.iconPushed = false;
      if (toSpec && toSpec->extras().content) toSpec->extras().content->restart();
    }
    advanceScroll(slotB_, toSpec, nowMs, 0);
    advanceIcons(slotB_, nowMs);

    if (!prepareFrames()) { renderPage(out, renderId, nowMs, kind, &slotA_); return; }
    renderPage(*transA_, ah.idAt(ah.currentIndex()), nowMs, kind, &slotA_);
    renderPage(*transB_, toId, nowMs, kind, &slotB_);
    const long perTrans = s.transitionDurationMs > 0 ? s.transitionDurationMs : kDefaultTransMs;
    const float p = static_cast<float>(nowMs - ah.phaseStartMs()) / perTrans;
    // Seeding on the phase start keeps a Random transition on one pick for its whole run.
    const Transition effect =
        render::resolveTransition(s.transitionEffect, static_cast<uint32_t>(ah.phaseStartMs()));
    const int direction =
        s.transitionDirection == kTransitionReverse ? -ah.direction() : ah.direction();
    render::composeTransition(out, *transA_, *transB_, effect, p, direction);
  } else {
    if (slotB_.icon && (!slotB_.pageId.empty() || !slotB_.iconId.empty() || slotB_.valid))
      slotB_.icon->clear();
    slotB_.pageId.clear();
    slotB_.iconId.clear();
    slotB_.valid = slotB_.missing = false;
    slotB_.retryAtMs = 0;
    slotB_.placedIcons.reset();
    slotB_.placedIconCount = 0;
    slotB_.placedRetryAtMs = 0;
    renderPage(out, renderId, nowMs, kind, &slotA_);
  }

  drawIndicators(out, nowMs);
  {
    const RuntimeState& rt = d_.engine->state().runtime();
    drawLinkStatus(out, rt.wifi, rt.mqtt, nowMs);
  }

  const bool native = spec && spec->extras().content;
  const bool repeating = spec && (native ? slotA_.contentFrame.wantsMoreTime : slotA_.scroll.wantsMoreTime(spec->repeat));
  const bool passesDone = spec && (native ? slotA_.contentFrame.passesDone : slotA_.scroll.passesDone(spec->repeat));
  d_.engine->setRotationHold(isApp && repeating);
  d_.engine->setNotificationHold(isNotif && repeating);
  d_.engine->setNotificationPassesDone(d_.engine->notifications().generation(),
                                       isNotif && passesDone);
  d_.engine->setRotationPassesDone(renderId, isApp && passesDone,
                                  native ? spec->extras().content->revision() : 0);
}

}
