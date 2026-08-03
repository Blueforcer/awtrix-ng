
#include <base64.hpp>

#include <cstring>
#include <vector>

#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "media/AssetFile.h"
#include "media/IconRenderer.h"
#include "system/Log.h"
#include "vendor/tjpgd/tjpgd.h"

namespace awtrix {
namespace icon {

namespace {

struct JpegCtx {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t pos = 0;
  Canvas* canvas = nullptr;
  int ox = 0;
  int oy = 0;
};

size_t jpgIn(JDEC* jd, uint8_t* buf, size_t len) {
  auto* ctx = static_cast<JpegCtx*>(jd->device);
  const size_t avail = ctx->size - ctx->pos;
  if (len > avail) len = avail;
  if (buf) std::memcpy(buf, ctx->data + ctx->pos, len);
  ctx->pos += len;
  return len;
}

int jpgOut(JDEC* jd, void* bitmap, JRECT* rect) {
  auto* ctx = static_cast<JpegCtx*>(jd->device);
  const auto* px = static_cast<const uint16_t*>(bitmap);
  for (int y = rect->top; y <= rect->bottom; ++y)
    for (int x = rect->left; x <= rect->right; ++x)
      ctx->canvas->setPixel(ctx->ox + x, ctx->oy + y, color::from565(*px++));
  return 1;
}

}

// Host half of icon::draw. The device goes through the Arduino TJpg_Decoder wrapper; here tjpgd is
// driven directly, and the two must agree on pixel placement or the sim lies about what you'd see.
bool draw(Canvas& canvas, const std::string& iconId, int x, int y) {
  media::PodBuffer<uint8_t> buf;
  // Longer than any legal icon name, so treat it as an inline base64 JPEG rather than a lookup.
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
    const std::string path = "/ICONS/" + iconId + ".jpg";
    if (!media::readAsset(path, buf)) {
      logf("icon: %s missing or empty", path.c_str());
      return false;
    }
  }

  JpegCtx ctx;
  ctx.data = buf.data();
  ctx.size = buf.size();
  ctx.canvas = &canvas;
  ctx.ox = x;
  ctx.oy = y;
  JDEC jd;
  uint8_t work[TJPGD_WORKSPACE_SIZE] __attribute__((aligned(4)));
  if (jd_prepare(&jd, jpgIn, work, sizeof(work), &ctx) != JDR_OK) return false;
  jd.swap = 0;
  return jd_decomp(&jd, jpgOut, 0) == JDR_OK;
}

}
}
