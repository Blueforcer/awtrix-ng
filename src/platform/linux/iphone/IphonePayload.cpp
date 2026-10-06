#include "core/render/TextEncoding.h"
#include "platform/linux/iphone/IphonePayload.h"

#include <algorithm>

#include "core/api/JsonWriter.h"

namespace awtrix::iphone {
namespace {

using api::JsonWriter;

bool blank(const std::string& text) {
  return std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
}

JsonWriter& box(JsonWriter& w, int x, int y, int width, int height) {
  return w.key("box").beginArray().value(x).value(y).value(width).value(height).endArray();
}

}

std::string clip(const std::string& text, std::size_t chars) {
  return std::string(awtrix::text::clipCodepoints(text, chars));
}

std::string content(const std::string& title, const std::string& message) {
  std::string out;
  for (const std::string* part : {&title, &message}) {
    if (blank(*part)) continue;
    if (!out.empty()) out += ": ";
    out += *part;
  }
  std::replace_if(out.begin(), out.end(), [](char c) { return c == '\r' || c == '\n' || c == '\t'; }, ' ');
  return out;
}

std::string notification(const Panel& panel, const std::string& app, const std::string& content,
                         const std::string& icon) {
  std::string out;
  JsonWriter w(out);
  w.beginObject().key("durationMs").value(kNotificationSeconds * 1000).key("repeat").value(1)
      .key("stack").value(true).key("wakeup").value(false);
  const bool empty = blank(content);
  const std::string message = clip(empty ? app : content, kMaxText);
  if (!panel.twoLines()) {
    w.key("text").value(!icon.empty() || empty ? message : clip(app + " · " + content, kMaxText));
    w.key("textColor").value(kMessageColor);
    if (!icon.empty()) w.key("icon").value(icon);
    w.endObject();
    return out;
  }
  const int size = panel.iconSize();
  const int x = icon.empty() ? 0 : size + 1;
  const int top = (panel.height - 16) / 2;
  w.key("layout").beginObject().key("version").value(1).key("regions").beginArray();
  if (!icon.empty()) {
    w.beginObject().key("id").value("icon");
    box(w, 0, (panel.height - size) / 2, size, size).key("icon").value(icon).endObject();
  }
  if (!empty) {
    w.beginObject().key("id").value("app");
    box(w, x, top, panel.width - x, 8).key("text").value(clip(app, 64)).key("color").value(kAppColor)
        .key("repeat").value(0);
    if (panel.appFont) w.key("font").value(kAppFont);
    w.endObject();
  }
  w.beginObject().key("id").value("message");
  box(w, x, empty ? (panel.height - 8) / 2 : top + 8, panel.width - x, 8).key("text").value(message)
      .key("color").value(kMessageColor).endObject();
  w.endArray().endObject().endObject();
  return out;
}

int Track::progress() const {
  if (durationMs <= 0) return -1;
  return static_cast<int>(std::min<int64_t>(100, std::max<int64_t>(0, positionMs * 100 / durationMs)));
}

std::string music(const Panel& panel, const Track& track, const std::string& cover) {
  std::string out;
  JsonWriter w(out);
  w.beginObject().key("lifetimeMs").value(kMusicLifetimeMs);
  const std::string title = clip(track.title, kMaxText);
  const std::string artist = clip(track.artist, kMaxText);
  const int progress = track.progress();
  if (!panel.twoLines()) {
    w.key("text").beginArray().beginObject().key("text").value(title).key("color").value(kTitleColor).endObject();
    if (!blank(artist))
      w.beginObject().key("text").value(" · " + artist).key("color").value(kAppColor).endObject();
    w.endArray();
    if (!cover.empty()) w.key("icon").value(cover);
    if (progress >= 0)
      w.key("progress").value(progress).key("progressColor").value(kMessageColor).key("progressTrackColor")
          .value(kTrackColor);
    w.endObject();
    return out;
  }
  const int size = panel.iconSize();
  const int x = cover.empty() ? 0 : size + 2;
  const int width = panel.width - x;
  const int top = (panel.height - 16) / 2;
  w.key("layout").beginObject().key("version").value(1).key("regions").beginArray();
  if (!cover.empty()) {
    w.beginObject().key("id").value("cover");
    box(w, 0, (panel.height - size) / 2, size, size).key("icon").value(cover).endObject();
  }
  w.beginObject().key("id").value("title");
  box(w, x, blank(artist) ? top + 4 : top, width, 8).key("text").value(title).key("color").value(kTitleColor)
      .endObject();
  if (!blank(artist)) {
    w.beginObject().key("id").value("artist");
    box(w, x, top + 8, width, 7).key("text").value(artist).key("color").value(kAppColor);
    if (panel.appFont) w.key("font").value(kAppFont);
    w.endObject();
  }
  if (progress >= 0) {
    w.beginObject().key("id").value("progress");
    box(w, x, top + 15, width, 1).key("progress").value(progress).key("color").value(kMessageColor)
        .key("trackColor").value(kTrackColor).endObject();
  }
  w.endArray().endObject().endObject();
  return out;
}

int barWidth(const Panel& panel) { return panel.twoLines() ? panel.width - panel.iconSize() - 2 : panel.width; }

int barLeds(const Track& track, int width) {
  const int progress = track.progress();
  return progress < 0 ? -1 : progress * width / 100;
}

}
