#include "../support.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <jpeglib.h>

#include "platform/linux/images/JpegPicture.h"

using namespace awtrix;
using images::DecodeFailure;

namespace {

constexpr auto check = awtrix::test::check;

// A solid-colour JPEG made by the same libjpeg, luma sampled h x v against chroma. scans > 0
// writes that many progressive scans: one DC scan, then one AC coefficient per component and scan.
std::vector<uint8_t> encode(int width, int height, bool progressive, int h = 2, int v = 2, int scans = 0) {
  jpeg_compress_struct info;
  jpeg_error_mgr errors;
  info.err = jpeg_std_error(&errors);
  jpeg_create_compress(&info);
  unsigned char* buffer = nullptr;
  unsigned long size = 0;
  jpeg_mem_dest(&info, &buffer, &size);
  info.image_width = static_cast<JDIMENSION>(width);
  info.image_height = static_cast<JDIMENSION>(height);
  info.input_components = 3;
  info.in_color_space = JCS_RGB;
  jpeg_set_defaults(&info);
  jpeg_set_quality(&info, 80, TRUE);
  info.comp_info[0].h_samp_factor = h;
  info.comp_info[0].v_samp_factor = v;
  std::vector<jpeg_scan_info> script;
  if (scans > 0) {
    script.push_back({3, {0, 1, 2, 0}, 0, 0, 0, 0});
    for (int k = 1; static_cast<int>(script.size()) < scans; ++k)
      for (int component = 0; component < 3 && static_cast<int>(script.size()) < scans; ++component)
        script.push_back({1, {component, 0, 0, 0}, k, k, 0, 0});
    info.scan_info = script.data();
    info.num_scans = static_cast<int>(script.size());
  } else if (progressive) {
    jpeg_simple_progression(&info);
  }
  jpeg_start_compress(&info, TRUE);
  std::vector<JSAMPLE> row(static_cast<std::size_t>(width) * 3);
  for (int x = 0; x < width; ++x) {
    row[x * 3] = 200;
    row[x * 3 + 1] = 40;
    row[x * 3 + 2] = 20;
  }
  JSAMPROW rows[1] = {row.data()};
  while (info.next_scanline < info.image_height) jpeg_write_scanlines(&info, rows, 1);
  jpeg_finish_compress(&info);
  std::vector<uint8_t> out(buffer, buffer + size);
  jpeg_destroy_compress(&info);
  std::free(buffer);
  return out;
}

bool near(uint32_t actual, uint32_t expected) {
  for (int shift : {16, 8, 0}) {
    const int a = static_cast<int>((actual >> shift) & 0xFF), e = static_cast<int>((expected >> shift) & 0xFF);
    if (std::abs(a - e) > 12) return false;
  }
  return true;
}

bool decodes(const std::vector<uint8_t>& jpeg) {
  media::RemoteImage image;
  return images::decodeJpeg(jpeg.data(), jpeg.size(), 16, 16, image) == DecodeFailure::None &&
         image.width == 16 && image.height == 16 && image.pixels.size() == 256 && near(image.pixels[0], 0xC82814) &&
         near(image.pixels[255], 0xC82814);
}

DecodeFailure failure(const std::vector<uint8_t>& jpeg) {
  media::RemoteImage image;
  return images::decodeJpeg(jpeg.data(), jpeg.size(), 16, 16, image);
}

}

int main() {
  check(decodes(encode(640, 640, false)), "a baseline JPEG decodes");
  check(decodes(encode(640, 640, true)), "a progressive JPEG decodes");
  check(decodes(encode(1920, 1280, true)), "a progressive 1920 x 1280 JPEG fits the decoder's memory");
  check(decodes(encode(1600, 1200, true, 2, 1)), "a progressive 1600 x 1200 JPEG at 4:2:2 decodes");
  check(failure(encode(1920, 1280, true, 2, 1)) == DecodeFailure::TooManyPixels,
        "a progressive 1920 x 1280 JPEG at 4:2:2 is beyond the decoder's memory");
  check(decodes(encode(1000, 1000, true, 1, 1)), "a progressive 1000 x 1000 JPEG without colour subsampling decodes");
  check(failure(encode(3000, 3000, true)) == DecodeFailure::TooManyPixels,
        "a progressive JPEG beyond the decoder's memory is refused");
  check(decodes(encode(4000, 4000, false)), "a large baseline JPEG still decodes without that memory");
  check(decodes(encode(64, 64, true, 2, 2, images::kMaxJpegScans)), "a JPEG with the most scans allowed decodes");
  check(failure(encode(64, 64, true, 2, 2, images::kMaxJpegScans + 1)) == DecodeFailure::TooManyPixels,
        "a JPEG with more scans is refused");
  const std::vector<uint8_t> progressive = encode(640, 640, true);
  const std::vector<uint8_t> header(progressive.begin(), progressive.begin() + 200);
  check(failure(header) == DecodeFailure::Format, "a JPEG cut in its header is refused");
  for (const bool scans : {false, true}) {
    const std::vector<uint8_t> whole = encode(640, 640, scans);
    const std::vector<uint8_t> cut(whole.begin(), whole.begin() + whole.size() * 3 / 4);
    check(failure(cut) == DecodeFailure::Format,
          scans ? "a progressive JPEG cut in its scans is refused" : "a baseline JPEG cut in its data is refused");
  }
  const uint8_t text[] = {0xFF, 0xD8, 'n', 'o', 't'};
  check(failure(std::vector<uint8_t>(text, text + sizeof text)) == DecodeFailure::Format, "a broken JPEG is refused");
  return awtrix::test::finish("jpeg");
}
