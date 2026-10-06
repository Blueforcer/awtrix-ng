#pragma once

#include <algorithm>
#include <cstring>
#include <limits>

#include "core/icons/IconSource.h"
#include "media/AssetFile.h"

namespace awtrix::media {

enum class IconRead : uint8_t { kGood, kMissing, kOom };

namespace detail {
inline const char* extension(icons::ImageFormat format) {
  return format == icons::ImageFormat::kGif ? ".gif" : ".jpg";
}

inline std::string_view iconPath(const icons::Source& source, icons::ImageFormat format,
                                 char (&path)[76]) {
  const std::string_view name = source.value;
  std::memcpy(path, "/ICONS/", 7);
  std::memcpy(path + 7, name.data(), name.size());
  std::memcpy(path + 7 + name.size(), extension(format), 5);
  return {path, 11 + name.size()};
}

inline IconRead decodeInline(const icons::Source& source, std::size_t limit,
                             PodBuffer<uint8_t>& out) {
  const char* text = source.value.data();
  const std::size_t length = source.value.size();
  const std::size_t capacity = base64::decodedSize(text, length);
  if (!capacity || capacity > limit) return IconRead::kMissing;
  if (!out.resize(capacity, capacity)) return IconRead::kOom;
  std::size_t written = 0;
  if (!base64::decode(text, length, out.data(), written) || !written) {
    out.clear();
    return IconRead::kMissing;
  }
  out.resize(written);
  return IconRead::kGood;
}

struct ImageProbe {
  std::string_view path;
  PodBuffer<uint8_t> inlineBytes;
  const uint8_t* bytes = nullptr;
  std::size_t size = 0;
  bool initialized = false;

  bool read(std::size_t offset, uint8_t* out, std::size_t count) {
    if (bytes) {
      if (offset > size || count > size - offset) return false;
      std::memcpy(out, bytes + offset, count);
      return true;
    }
    if (!inlineBytes.empty()) {
      if (offset > inlineBytes.size() || count > inlineBytes.size() - offset) return false;
      std::memcpy(out, inlineBytes.data() + offset, count);
      size = inlineBytes.size();
      return true;
    }
    std::size_t got = 0, length = 0;
    if (!readAssetRange(path, offset, out, count, got, length) || got != count) return false;
    if (initialized && length != size) return false;
    size = length;
    initialized = true;
    return true;
  }
};

inline bool imageHeader(ImageProbe& probe, int& width, int& height, bool& gif) {
  uint8_t h[13];
  if (!probe.read(0, h, sizeof(h))) return false;
  gif = !std::memcmp(h, "GIF87a", 6) || !std::memcmp(h, "GIF89a", 6);
  if (gif) {
    width = h[6] | (h[7] << 8);
    height = h[8] | (h[9] << 8);
    return width > 0 && height > 0;
  }
  if (h[0] != 0xFF || h[1] != 0xD8) return false;
  std::size_t offset = 2;
  for (int segment = 0; segment < 256; ++segment) {
    if (!probe.read(offset, h, 4) || h[0] != 0xFF) return false;
    if (h[1] == 0xFF) { ++offset; continue; }
    const std::size_t length = (h[2] << 8) | h[3];
    if (length < 2 || offset + 2 > probe.size || length > probe.size - offset - 2) return false;
    if (h[1] == 0xC0) {
      if (length < 8 || !probe.read(offset + 4, h, 6) || h[0] != 8 ||
          (h[5] != 1 && h[5] != 3)) return false;
      height = (h[1] << 8) | h[2];
      width = (h[3] << 8) | h[4];
      return width > 0 && height > 0;
    }
    if (h[1] == 0xDA || h[1] == 0xD9 || h[1] == 0xC2) return false;
    offset += 2 + length;
  }
  return false;
}

inline bool residentCost(std::size_t compressed, int width, int height, bool gif,
                         std::size_t& result) {
  const auto count = static_cast<std::size_t>(width) * height;
  // GIF: frame (4 B/px), restore growth including old+new buffers (12 B/px),
  // LZW dictionary/stack/indices (5 B/px). JPEG: RGB + decoder workspace.
  const auto fixed = gif ? 512u : 4096u;
  const auto perPixel = gif ? 21u : 4u;
  if (count > (std::numeric_limits<std::size_t>::max() - fixed) / perPixel ||
      compressed > std::numeric_limits<std::size_t>::max() - fixed - count * perPixel) return false;
  result = compressed + count * perPixel + fixed;
  return true;
}
}

// A file source reads /ICONS/<name>.gif or .jpg; an inline source offers only its declared format.
inline IconRead readIconBytes(const icons::Source& source, icons::ImageFormat format,
                              PodBuffer<uint8_t>& out,
                              std::size_t limit = std::numeric_limits<std::size_t>::max()) {
  out.clear();
  if (!limit || !source.offers(format)) return IconRead::kMissing;
  if (source.inlined()) return detail::decodeInline(source, limit, out);
  char storage[76];
  const std::string_view path = detail::iconPath(source, format, storage);
  if (limit == std::numeric_limits<std::size_t>::max()) {
    bool oom = false;
    if (readAsset(path, out, &oom)) return IconRead::kGood;
    return oom ? IconRead::kOom : IconRead::kMissing;
  }
  uint8_t first;
  std::size_t got = 0, size = 0, actual = 0;
  if (!readAssetRange(path, 0, &first, 1, got, size) || got != 1 || size > limit)
    return IconRead::kMissing;
  if (!out.resize(size, size)) return IconRead::kOom;
  if (!readAssetRange(path, 0, out.data(), size, got, actual) || got != size || actual != size) {
    out.clear();
    return IconRead::kMissing;
  }
  return IconRead::kGood;
}

inline bool inspectImageBytes(const uint8_t* bytes, std::size_t size,
                              int& width, int& height, bool& gif) {
  detail::ImageProbe probe;
  probe.bytes = bytes;
  probe.size = size;
  return detail::imageHeader(probe, width, height, gif);
}

// Native admission decodes these exact bytes. A replaced file cannot grow the allocation
// beyond its reservation or change dimensions between inspection and decoding.
inline IconRead readNativeImage(const icons::Source& source, icons::ImageFormat format,
                                int maxWidth, int maxHeight, std::size_t limit,
                                PodBuffer<uint8_t>& bytes, int& width, int& height) {
  width = height = 0;
  const IconRead read = readIconBytes(source, format, bytes, limit);
  if (read != IconRead::kGood) return read;
  bool gif = false;
  std::size_t cost = 0;
  if (inspectImageBytes(bytes.data(), bytes.size(), width, height, gif) &&
      gif == (format == icons::ImageFormat::kGif) && width <= maxWidth && height <= maxHeight &&
      detail::residentCost(bytes.size(), width, height, gif, cost) && cost <= limit)
    return IconRead::kGood;
  bytes.clear();
  width = height = 0;
  return IconRead::kMissing;
}

inline bool inspectNative(std::string_view id, int maxWidth, int maxHeight,
                          int& width, int& height, std::size_t& residentBytes) {
  width = height = 0;
  residentBytes = 0;
  if (maxWidth <= 0 || maxHeight <= 0) return false;
  const icons::Source source = icons::parse(id);
  for (const auto format : {icons::ImageFormat::kGif, icons::ImageFormat::kJpeg}) {
    if (!source.offers(format)) continue;
    detail::ImageProbe probe;
    char path[76];
    if (source.inlined()) {
      if (detail::decodeInline(source, 64 * 1024, probe.inlineBytes) != IconRead::kGood)
        return false;
    } else {
      probe.path = detail::iconPath(source, format, path);
    }
    bool gif = false;
    int w = 0, h = 0;
    if (!detail::imageHeader(probe, w, h, gif) || gif != (format == icons::ImageFormat::kGif) ||
        w > maxWidth || h > maxHeight) continue;
    if (!detail::residentCost(probe.size, w, h, gif, residentBytes)) return false;
    width = w;
    height = h;
    return true;
  }
  return false;
}

}
