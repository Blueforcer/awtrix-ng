#pragma once

#include <cstddef>
#include <cstdint>

#include "media/RemoteImages.h"
#include "platform/linux/images/PictureDecoder.h"

namespace awtrix::images {

// Fits a non-interlaced PNG of any colour type and bit depth to exactly width x height, row by
// row as it unpacks. Transparency shows as black.
DecodeFailure decodePng(const uint8_t* data, std::size_t size, int width, int height, media::RemoteImage& out);

}
