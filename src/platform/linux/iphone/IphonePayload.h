#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The notifications and the now-playing app an iPhone's link puts on the display, built as the
// Android companion builds them, so either kind of phone looks the same there.
namespace awtrix::iphone {

// What the payloads need to know about the display.
struct Panel {
  int width = 32;
  int height = 8;
  bool layouts = false;
  // The font the app line is set in is there.
  bool appFont = false;
  bool twoLines() const { return layouts && height >= 16 && width >= 32; }
  int iconSize() const { return twoLines() ? 16 : 8; }
};

constexpr const char* kMessageColor = "#F5A568";
constexpr int kNotificationSeconds = 8;
constexpr const char* kAppColor = "#9A9A9A";
constexpr const char* kAppFont = "matrix-light6";
constexpr const char* kTitleColor = "#FFFFFF";
constexpr const char* kTrackColor = "#202020";
constexpr const char* kMusicApp = "nowplaying";
constexpr std::size_t kMaxText = 240;
constexpr long kMusicLifetimeMs = 65000;

// At most chars UTF-8 characters of text.
std::string clip(const std::string& text, std::size_t chars);

// "Title: Message", or whichever of the two is there; line breaks read as spaces.
std::string content(const std::string& title, const std::string& message);

// One notification: the icon, the app's name and the content; the app's name alone when there is
// no content, shown for kNotificationSeconds. An empty icon means none.
std::string notification(const Panel& panel, const std::string& app, const std::string& content,
                         const std::string& icon);

struct Track {
  std::string title, artist;
  int64_t durationMs = 0;
  int64_t positionMs = 0;
  // Percent played, or -1 without a duration.
  int progress() const;
};

// The pushed app kMusicApp: title, artist, the cover and the progress bar.
std::string music(const Panel& panel, const Track& track, const std::string& cover);

// The progress bar's width in LEDs, and how many of them the track lights (-1 without a bar).
int barWidth(const Panel& panel);
int barLeds(const Track& track, int width);

}
