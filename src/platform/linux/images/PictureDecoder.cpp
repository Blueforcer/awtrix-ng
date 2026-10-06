#include "platform/linux/images/PictureDecoder.h"

#include <algorithm>
#include <cstring>

#include "core/AssetPaths.h"
#include "media/ImageInfo.h"
#include "platform/linux/images/JpegPicture.h"
#include "platform/linux/images/PictureFit.h"
#include "platform/linux/images/PngPicture.h"

namespace awtrix::images {
namespace {

// Played as it is by the display, which decodes it there; here only its size is judged.
DecodeFailure keepGif(const uint8_t* data, std::size_t size, int width, int height,
                      int gifWidth, int gifHeight, media::RemoteImage& out) {
  if (gifWidth > width || gifHeight > height || size > media::kMaxRemoteGifBytes) return DecodeFailure::GifTooLarge;
  if (!out.gif.resize(size, size)) return DecodeFailure::Memory;
  std::memcpy(out.gif.data(), data, size);
  out.width = gifWidth;
  out.height = gifHeight;
  return DecodeFailure::None;
}

}

DecodeFailure decodePicture(const uint8_t* data, std::size_t size, int width, int height,
                            media::RemoteImage& out) {
  out = media::RemoteImage{};
  if (width <= 0 || height <= 0 || width > PictureFit::kMaxSide || height > PictureFit::kMaxSide)
    return DecodeFailure::Unfit;
  if (!data || !assets::looksLikeImage(data, static_cast<unsigned>(std::min<std::size_t>(size, 4))))
    return DecodeFailure::Format;
  if (data[0] == 0x89)
    return decodePng(data, size, width, height, out);
  if (data[0] == 0xFF) return decodeJpeg(data, size, width, height, out);
  int sourceWidth = 0, sourceHeight = 0;
  bool gif = false;
  if (!media::inspectImageBytes(data, size, sourceWidth, sourceHeight, gif) || !gif)
    return DecodeFailure::Format;
  return keepGif(data, size, width, height, sourceWidth, sourceHeight, out);
}

}
