#pragma once

#include <algorithm>
#include <iterator>
#include <string_view>

namespace awtrix::tc002 {

enum class ClipKind { Wav, Mp3 };

struct ClipFormat {
  ClipKind kind;
  const char* mediaType;
  const char* extension;
};

inline constexpr ClipFormat kClipFormats[] = {{ClipKind::Wav, "audio/wav", "wav"},
                                              {ClipKind::Mp3, "audio/mpeg", "mp3"}};

inline const ClipFormat& clipFormat(ClipKind kind) {
  return *std::find_if(std::begin(kClipFormats), std::end(kClipFormats),
                       [kind](const ClipFormat& format) { return format.kind == kind; });
}

inline const ClipFormat* clipFormatOf(std::string_view mediaType) {
  const auto it = std::find_if(std::begin(kClipFormats), std::end(kClipFormats),
                               [mediaType](const ClipFormat& format) { return mediaType == format.mediaType; });
  return it == std::end(kClipFormats) ? nullptr : it;
}

}
