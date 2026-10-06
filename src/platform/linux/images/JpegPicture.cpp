#include "platform/linux/images/JpegPicture.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>

#include <jpeglib.h>
#include <jerror.h>

#include "platform/linux/images/PictureFit.h"

namespace awtrix::images {
namespace {

constexpr int kMaxJpegSide = 8192;

// libjpeg reports a fatal error through error_exit, which must not return: it jumps back to
// unpack(). Only trivially destructible objects live between setjmp and that jump.
struct Errors {
  jpeg_error_mgr base;
  std::jmp_buf jump;
  DecodeFailure failure = DecodeFailure::Format;
};

[[noreturn]] void stop(j_common_ptr info, DecodeFailure failure) {
  Errors* errors = reinterpret_cast<Errors*>(info->err);
  errors->failure = failure;
  std::longjmp(errors->jump, 1);
}

// Beyond max_memory_to_use libjpeg asks for a backing store, which this build has none of.
[[noreturn]] void fail(j_common_ptr info) {
  const int code = info->err->msg_code;
  stop(info, code == JERR_NO_BACKING_STORE ? DecodeFailure::TooManyPixels
             : code == JERR_OUT_OF_MEMORY  ? DecodeFailure::Memory
                                           : DecodeFailure::Format);
}

// Warnings pass, except the one for data that ends early: a cut picture is not shown.
void warn(j_common_ptr info, int level) {
  if (level < 0 && info->err->msg_code == JWRN_JPEG_EOF) stop(info, DecodeFailure::Format);
}

void limitScans(j_common_ptr info) {
  if (reinterpret_cast<j_decompress_ptr>(info)->input_scan_number > kMaxJpegScans)
    stop(info, DecodeFailure::TooManyPixels);
}

// Whether the middle cut of a source of that size, in the target's shape, still has every
// target pixel's worth.
bool coversTarget(int sourceWidth, int sourceHeight, int width, int height) {
  const int64_t cutWidth = std::min<int64_t>(sourceWidth, static_cast<int64_t>(sourceHeight) * width / height);
  const int64_t cutHeight = std::min<int64_t>(sourceHeight, static_cast<int64_t>(sourceWidth) * height / width);
  return cutWidth >= width && cutHeight >= height;
}

DecodeFailure unpack(const uint8_t* data, std::size_t size, int width, int height, PictureFit& fit) {
  jpeg_decompress_struct info{};
  Errors errors;
  jpeg_progress_mgr progress{};
  info.err = jpeg_std_error(&errors.base);
  errors.base.error_exit = fail;
  errors.base.emit_message = warn;
  if (setjmp(errors.jump)) {
    jpeg_destroy_decompress(&info);
    return errors.failure;
  }
  jpeg_create_decompress(&info);
  info.mem->max_memory_to_use = kMaxJpegMemory;
  progress.progress_monitor = limitScans;
  info.progress = &progress;
  jpeg_mem_src(&info, data, static_cast<unsigned long>(size));
  if (jpeg_read_header(&info, TRUE) != JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&info);
    return DecodeFailure::Format;
  }
  if (info.image_width > kMaxJpegSide || info.image_height > kMaxJpegSide) {
    jpeg_destroy_decompress(&info);
    return DecodeFailure::TooManyPixels;
  }
  if (info.jpeg_color_space == JCS_CMYK || info.jpeg_color_space == JCS_YCCK) {
    jpeg_destroy_decompress(&info);
    return DecodeFailure::Format;
  }
  info.out_color_space = JCS_RGB;
  info.scale_num = 1;
  // The strongest reduction that still leaves enough pixels to average.
  for (info.scale_denom = 8;; info.scale_denom /= 2) {
    jpeg_calc_output_dimensions(&info);
    if (info.scale_denom == 1 || coversTarget(static_cast<int>(info.output_width),
                                              static_cast<int>(info.output_height), width, height))
      break;
  }
  if (!fit.begin(static_cast<int>(info.output_width), static_cast<int>(info.output_height), width, height)) {
    jpeg_destroy_decompress(&info);
    return DecodeFailure::TooManyPixels;
  }
  jpeg_start_decompress(&info);
  JSAMPARRAY row = (*info.mem->alloc_sarray)(reinterpret_cast<j_common_ptr>(&info), JPOOL_IMAGE,
                                             info.output_width * 3, 1);
  while (info.output_scanline < info.output_height) {
    const int y = static_cast<int>(info.output_scanline);
    jpeg_read_scanlines(&info, row, 1);
    const JSAMPLE* pixel = row[0];
    for (JDIMENSION x = 0; x < info.output_width; ++x, pixel += 3)
      fit.add(static_cast<int>(x), y, pixel[0], pixel[1], pixel[2]);
  }
  jpeg_finish_decompress(&info);
  jpeg_destroy_decompress(&info);
  return DecodeFailure::None;
}

}

DecodeFailure decodeJpeg(const uint8_t* data, std::size_t size, int width, int height, media::RemoteImage& out) {
  PictureFit fit;
  const DecodeFailure failure = unpack(data, size, width, height, fit);
  if (failure != DecodeFailure::None) return failure;
  if (!fit.finish(out.pixels)) return DecodeFailure::Memory;
  out.width = width;
  out.height = height;
  return DecodeFailure::None;
}

}
