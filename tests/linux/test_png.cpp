#include "../support.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/linux/images/Inflate.h"
#include "platform/linux/images/PictureDecoder.h"
#include "platform/linux/images/PngPicture.h"
#include "png_fixtures.h"

using namespace awtrix;
using images::DecodeFailure;
using media::RemoteImage;

namespace {

constexpr auto check = awtrix::test::require;

std::string streamData(std::size_t length) {
  const std::string words = "album cover now playing kitchen speaker ";
  std::string out;
  for (std::size_t i = 0; i < length; ++i) out += words[(i * 7 + i / 97) % words.size()];
  return out;
}

bool inflateTo(const uint8_t* data, std::size_t size, std::string& out) {
  out.clear();
  return images::inflateZlib(data, size, [&](const uint8_t* bytes, std::size_t count) {
    out.append(reinterpret_cast<const char*>(bytes), count);
    return true;
  });
}

bool near(uint32_t actual, uint32_t expected, int tolerance = 12) {
  for (int shift : {16, 8, 0}) {
    const int a = (actual >> shift) & 0xFF, e = (expected >> shift) & 0xFF;
    if (a - e > tolerance || e - a > tolerance) return false;
  }
  return true;
}

uint32_t at(const RemoteImage& image, int x, int y) { return image.pixels[static_cast<std::size_t>(y) * image.width + x]; }

bool quadrants(const RemoteImage& image) {
  return image.width == 16 && image.height == 16 && near(at(image, 2, 2), 0xFF0000) &&
         near(at(image, 13, 2), 0x00FF00) && near(at(image, 2, 13), 0x0000FF) && near(at(image, 13, 13), 0xFFFFFF);
}

void testInflate() {
  std::string out;
  check(inflateTo(kStoredZlib, sizeof(kStoredZlib), out) && out == streamData(1000), "stored blocks unpack");
  check(inflateTo(kFixedZlib, sizeof(kFixedZlib), out) && out == streamData(100000),
        "fixed codes unpack past the window");
  check(inflateTo(kDynamicZlib, sizeof(kDynamicZlib), out) && out == streamData(100000),
        "dynamic codes unpack past the window");
  const images::ByteSpan pieces[] = {{kDynamicZlib, 1}, {kDynamicZlib + 1, 0}, {kDynamicZlib + 1, 100},
                                     {kDynamicZlib + 101, sizeof(kDynamicZlib) - 101}};
  out.clear();
  check(images::inflateZlib(pieces, 4, [&](const uint8_t* bytes, std::size_t count) {
          out.append(reinterpret_cast<const char*>(bytes), count);
          return true;
        }) && out == streamData(100000),
        "a stream split into pieces, header included, unpacks the same");
  check(!inflateTo(kDynamicZlib, sizeof(kDynamicZlib) - 40, out), "a cut stream fails");
  const uint8_t badHeader[] = {0x78, 0x00, 0x03, 0x00};
  check(!inflateTo(badHeader, sizeof(badHeader), out), "a bad header fails");
  const uint8_t dictionary[] = {0x78, 0xBB, 0, 0, 0, 1};
  check(!inflateTo(dictionary, sizeof(dictionary), out), "a preset dictionary fails");
  std::size_t seen = 0;
  check(!images::inflateZlib(kDynamicZlib, sizeof(kDynamicZlib), [&](const uint8_t*, std::size_t count) {
          seen += count;
          return false;
        }) && seen > 0 && seen < 100000,
        "a sink can stop it");
}

// A 1x1 grey PNG whose one row is followed by about 100 MB of zeros: fixed Huffman codes, each
// match repeating 258 bytes at distance 1.
std::vector<uint8_t> zeroBombPng() {
  std::vector<uint8_t> stream = {0x78, 0x01};
  uint32_t buffer = 0;
  int count = 0;
  const auto bits = [&](uint32_t value, int n) {
    buffer |= value << count;
    count += n;
    while (count >= 8) {
      stream.push_back(static_cast<uint8_t>(buffer));
      buffer >>= 8;
      count -= 8;
    }
  };
  const auto code = [&](uint32_t value, int n) {
    for (int i = n - 1; i >= 0; --i) bits(value >> i & 1, 1);
  };
  bits(1, 1);
  bits(1, 2);
  code(0x30, 8);
  code(0x30, 8);
  for (int i = 0; i < 400000; ++i) {
    code(0xC5, 8);
    code(0, 5);
  }
  code(0, 7);
  bits(0, 7);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  const auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    const uint32_t n = static_cast<uint32_t>(data.size());
    png.insert(png.end(), {static_cast<uint8_t>(n >> 24), static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 8),
                           static_cast<uint8_t>(n)});
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    png.insert(png.end(), {0, 0, 0, 0});
  };
  chunk("IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 0, 0, 0, 0});
  chunk("IDAT", stream);
  chunk("IEND", {});
  return png;
}

void testPng() {
  RemoteImage image;
  check(images::decodePicture(kQuadrantsPng, sizeof(kQuadrantsPng), 16, 16, image) == DecodeFailure::None &&
            quadrants(image),
        "an RGB PNG keeps its middle square");
  check(images::decodePng(kLargeQuadrantsPng, sizeof(kLargeQuadrantsPng), 16, 16, image) == DecodeFailure::None &&
            quadrants(image),
        "a large PNG shrinks");
  check(images::decodePng(kPalettePng, sizeof(kPalettePng), 16, 16, image) == DecodeFailure::None && quadrants(image),
        "a 2-bit palette PNG decodes");
  check(images::decodePng(kTransparentPng, sizeof(kTransparentPng), 16, 16, image) == DecodeFailure::None &&
            at(image, 2, 2) == 0 && near(at(image, 13, 2), 0x00FF00) && near(at(image, 13, 13), 0xFFFFFF),
        "transparency shows as black");
  check(images::decodePng(kGray16Png, sizeof(kGray16Png), 16, 16, image) == DecodeFailure::None &&
            at(image, 7, 3) == 0 && at(image, 8, 3) == 0xFFFFFF,
        "16-bit grey takes its high byte");
  check(images::decodePng(kFilteredPng, sizeof(kFilteredPng), 16, 16, image) == DecodeFailure::None,
        "every row filter decodes");
  for (int i = 0; i < 256; ++i) {
    const uint32_t expected = static_cast<uint32_t>(kFilteredPixels[i * 3]) << 16 |
                              static_cast<uint32_t>(kFilteredPixels[i * 3 + 1]) << 8 | kFilteredPixels[i * 3 + 2];
    check(near(image.pixels[i], expected, 1), "filtered pixels come out as they went in");
  }

  std::vector<uint8_t> png(kQuadrantsPng, kQuadrantsPng + sizeof(kQuadrantsPng));
  png[28] = 1;
  check(images::decodePng(png.data(), png.size(), 16, 16, image) == DecodeFailure::Format, "interlaced PNGs fail");
  png.assign(kQuadrantsPng, kQuadrantsPng + sizeof(kQuadrantsPng));
  png[16] = 0, png[17] = 0, png[18] = 0x10, png[19] = 0;
  png[20] = 0, png[21] = 0, png[22] = 0x10, png[23] = 0;
  check(images::decodePng(png.data(), png.size(), 16, 16, image) == DecodeFailure::TooManyPixels,
        "a PNG beyond 2048x2048 pixels is too large");
  check(images::decodePng(kQuadrantsPng, sizeof(kQuadrantsPng) - 30, 16, 16, image) == DecodeFailure::Format,
        "a cut PNG fails");
  const std::vector<uint8_t> bomb = zeroBombPng();
  const auto started = std::chrono::steady_clock::now();
  check(images::decodePng(bomb.data(), bomb.size(), 4, 4, image) == DecodeFailure::None && image.pixels[0] == 0,
        "a PNG with a long tail decodes");
  check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(200),
        "unpacking stops after the last row");
  png.assign(kQuadrantsPng, kQuadrantsPng + sizeof(kQuadrantsPng));
  png[25] = 5;
  check(images::decodePng(png.data(), png.size(), 16, 16, image) == DecodeFailure::Format,
        "an unknown colour type fails");
}

}

int main() {
  testInflate();
  testPng();
  std::puts("png: ok");
  return 0;
}
