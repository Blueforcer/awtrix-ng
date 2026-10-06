#include "media/IconRenderer.h"

#include "media/AssetFile.h"
#include "media/ImageInfo.h"
#include "media/JpegDecoder.h"
#include "system/Log.h"

namespace awtrix {
namespace icon {

namespace {
struct JpegTarget {
  Canvas& canvas;
  int x, y;
};

void jpgOutput(void* target, int x, int y, uint32_t color) {
  auto& out = *static_cast<JpegTarget*>(target);
  out.canvas.setPixel(out.x + x, out.y + y, color);
}
}

bool draw(Canvas& canvas, std::string_view icon, int x, int y, bool* outOfMemory) {
  if (outOfMemory) *outOfMemory = false;
  const icons::Source source = icons::parse(icon);
  if (!source.offers(icons::ImageFormat::kJpeg)) {
    logf(source.inlined() ? "icon: bad inline GIF" : "icon: invalid icon name");
    return false;
  }
  const std::string label =
      source.inlined() ? std::string("inline JPEG") : "/ICONS/" + std::string(icon) + ".jpg";
  media::PodBuffer<uint8_t> buf;
  switch (media::readIconBytes(source, icons::ImageFormat::kJpeg, buf)) {
    case media::IconRead::kGood:
      break;
    case media::IconRead::kOom:
      if (outOfMemory) *outOfMemory = true;
      logf("icon: no memory for %s", label.c_str());
      return false;
    case media::IconRead::kMissing:
      logf("icon: %s missing or empty", label.c_str());
      return false;
  }

  if (isPng(buf.data(), buf.size())) {
    logf("icon: %s is a PNG, only GIF and JPEG are supported", label.c_str());
    return false;
  }

  media::JpegDecoder decoder;
  JpegTarget target{canvas, x, y};
  auto res = decoder.prepare(buf.data(), buf.size());
  if (res == JDR_OK) res = decoder.decode(jpgOutput, &target);
  if (res != JDR_OK) {
    if (outOfMemory && (res == JDR_MEM1 || res == JDR_MEM2)) *outOfMemory = true;
    logf("icon: %s is not a JPEG the decoder can read (%d)", label.c_str(),
         static_cast<int>(res));
    return false;
  }
  return true;
}

bool decodeNative(const uint8_t* bytes, std::size_t size, int maxWidth, int maxHeight,
                  media::PodBuffer<uint32_t>& pixels, int& width, int& height,
                  bool* outOfMemory) {
  width = height = 0;
  pixels.clear();
  if (outOfMemory) *outOfMemory = false;
  int w = 0, h = 0;
  bool gif = false;
  if (!media::inspectImageBytes(bytes, size, w, h, gif) || gif ||
      w > maxWidth || h > maxHeight) return false;
  const std::size_t count = static_cast<std::size_t>(w) * h;
  if (!pixels.resize(count, count)) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  Canvas canvas(w, h, pixels.data());
  canvas.clear();
  media::JpegDecoder decoder;
  JpegTarget target{canvas, 0, 0};
  auto result = decoder.prepare(bytes, size);
  if (result == JDR_OK && (decoder.width() != w || decoder.height() != h)) result = JDR_FMT1;
  if (result == JDR_OK) result = decoder.decode(jpgOutput, &target);
  if (result != JDR_OK) {
    if (outOfMemory && (result == JDR_MEM1 || result == JDR_MEM2)) *outOfMemory = true;
    pixels.clear();
    return false;
  }
  width = w;
  height = h;
  return true;
}

}
}
