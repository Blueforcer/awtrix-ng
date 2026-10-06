#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace awtrix::radio {

inline constexpr std::size_t kInputBufferBytes = 64 * 1024;
inline constexpr std::size_t kPrerollBytes = 16 * 1024;
inline constexpr std::size_t kUndecodableAfterBytes = 32 * 1024;
inline constexpr std::size_t kMaxPlaylistBytes = 4096;
inline constexpr int kMaxPlaylistHops = 3;
inline constexpr uint32_t kReadTimeoutMs = 8000;
inline constexpr uint32_t kBackoffMs[] = {2000, 5000, 15000};

inline bool isPlaylistType(const std::string& type) {
  return type.find("audio/x-mpegurl") != type.npos ||
         type.find("audio/mpegurl") != type.npos ||
         type.find("audio/x-scpls") != type.npos;
}

}
