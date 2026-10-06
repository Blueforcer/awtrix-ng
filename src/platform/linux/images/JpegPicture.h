#pragma once

#include <cstddef>
#include <cstdint>

#include "media/RemoteImages.h"
#include "platform/linux/images/PictureDecoder.h"

namespace awtrix::images {

// Fits a baseline or progressive JPEG to exactly width x height. It is reduced by 1/2, 1/4 or 1/8
// while decoding as far as the target still gets every pixel's worth. A picture whose decoding
// needs more than kMaxJpegMemory, or whose progressive scans exceed kMaxJpegScans, is TooManyPixels;
// one whose data ends early is Format.
DecodeFailure decodeJpeg(const uint8_t* data, std::size_t size, int width, int height, media::RemoteImage& out);

constexpr std::size_t kMaxJpegMemory = 8u * 1024 * 1024;
constexpr int kMaxJpegScans = 100;

}
