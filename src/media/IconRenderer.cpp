#include "media/IconRenderer.h"

#include <TJpg_Decoder.h>
#include <base64.hpp>

#include <vector>

#include "core/render/Color.h"
#include "media/AssetFile.h"
#include "system/Log.h"

namespace awtrix {
namespace icon {

namespace {
// TJpg_Decoder's output callback carries no user pointer, so the target canvas has to be parked
// here for the duration of the drawJpg() call. Not reentrant, and does not need to be.
Canvas* s_canvas = nullptr;

bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (!s_canvas) return false;
  for (uint16_t j = 0; j < h; ++j)
    for (uint16_t i = 0; i < w; ++i)
      s_canvas->setPixel(x + i, y + j, color::from565(bitmap[j * w + i]));
  return true;
}
}

bool draw(Canvas& canvas, const std::string& iconId, int x, int y) {
  media::PodBuffer<uint8_t> buf;
  std::string path;
  // Same convention as GifPlayer: anything longer than a plausible filename is inline base64.
  if (iconId.size() > 64) {
    const auto* in = reinterpret_cast<const unsigned char*>(iconId.c_str());
    const unsigned int maxLen = decode_base64_length(in, iconId.size());
    if (!buf.resize(maxLen)) {
      logf("icon: no memory for inline icon");
      return false;
    }
    const unsigned int n = decode_base64(in, iconId.size(), buf.data());
    if (n == 0) {
      logf("icon: inline base64 decode failed");
      return false;
    }
    buf.resize(n);
  } else {
    path = "/ICONS/" + iconId + ".jpg";
    if (!media::readAsset(path, buf)) {
      logf("icon: %s missing or empty", path.c_str());
      return false;
    }
  }

  const char* label = path.empty() ? "inline icon" : path.c_str();
  if (isPng(buf.data(), buf.size())) {
    logf("icon: %s is a PNG, only GIF and JPEG are supported", label);
    return false;
  }

  s_canvas = &canvas;
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(jpgOutput);
  const JRESULT res = TJpgDec.drawJpg(x, y, buf.data(), static_cast<uint32_t>(buf.size()));
  s_canvas = nullptr;
  if (res != JDR_OK) {
    logf("icon: %s is not a JPEG the decoder can read (%d)", label, static_cast<int>(res));
    return false;
  }
  return true;
}

}
}
