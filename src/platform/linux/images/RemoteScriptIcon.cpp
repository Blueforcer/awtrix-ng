#include "media/ScriptIcon.h"

#include <algorithm>
#include <new>

#include "media/GifPlayer.h"

namespace awtrix {

// A URL picture is a square as tall as the panel; a GIF smaller than that sits in its middle.
void ScriptIconSet::loadRemote(Entry& e, std::string_view url, int64_t nowMs) {
  e.state = State::kMissing;
  e.retryStep = 0;
  media::IRemoteImages* remote = service_.remote();
  const int side = std::min(service_.maxWidth(), service_.maxHeight());
  if (!remote || side <= 0) return;
  e.remoteSeen = remote->generation();
  media::RemoteImage image;
  if (remote->get(url, side, side, image) != media::RemoteState::kReady) {
    e.state = State::kPending;
    return;
  }
  if (image.gif.empty()) {
    e.pixels = std::move(image.pixels);
    e.width = image.width;
    e.height = image.height;
  } else {
    GifPlayer* gif = new (std::nothrow) GifPlayer();
    if (!gif) {
      outOfMemory(e, url, nowMs);
      return;
    }
    const GifPlayer::OpenResult r = gif->openBytes(std::move(image.gif), side, side, false, 1);
    if (r != GifPlayer::OpenResult::kGood) {
      delete gif;
      if (r == GifPlayer::OpenResult::kOom) outOfMemory(e, url, nowMs);
      return;
    }
    if (!adopt(e, gif, nowMs)) {
      outOfMemory(e, url, nowMs);
      return;
    }
  }
  e.padX = static_cast<uint8_t>((side - e.width) / 2);
  e.padY = static_cast<uint8_t>((side - e.height) / 2);
  e.state = State::kGood;
}

}
