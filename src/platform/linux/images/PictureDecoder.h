#pragma once

#include <cstddef>
#include <cstdint>

#include "media/RemoteImages.h"

namespace awtrix::images {

// Format: no picture this display reads. TooManyPixels: beyond what the decoder unpacks.
// GifTooLarge: a GIF larger than the target or than kMaxRemoteGifBytes. Unfit: a target of no or
// too many pixels.
enum class DecodeFailure : uint8_t { None, Format, TooManyPixels, GifTooLarge, Unfit, Memory };

// Fits an encoded picture, recognised by its content, to exactly width x height. A GIF that fits
// is kept as it is, so it can still play. Safe on any thread.
DecodeFailure decodePicture(const uint8_t* data, std::size_t size, int width, int height,
                            media::RemoteImage& out);

}
