#pragma once

#include <string>

#include "core/sound/Rtttl.h"

namespace awtrix {
namespace assets {

inline bool isAssetPath(const std::string& path) {
  if (path.find("..") != std::string::npos) return false;
  return path.rfind("/ICONS/", 0) == 0 || path.rfind("/MELODIES/", 0) == 0 ||
         path.rfind("/PALETTES/", 0) == 0 || path.rfind("/SOUNDS/", 0) == 0;
}

inline bool isServable(const std::string& path) { return isAssetPath(path); }
inline bool isWritable(const std::string& path) { return isAssetPath(path); }

// Wider than isServable on purpose: scripts and the app arrangement belong in a backup, but they
// are not handed out over the file endpoints.
inline bool isBackupReadable(const std::string& path) {
  if (path.find("..") != std::string::npos) return false;
  return path.rfind("/SCRIPTS/", 0) == 0 || path == "/apploop.json" || path == "/radio.json";
}

inline bool isBackupWritable(const std::string& path) {
  if (path.find("..") != std::string::npos) return false;
  return path.rfind("/ICONS/", 0) == 0 || path.rfind("/MELODIES/", 0) == 0 ||
         path.rfind("/PALETTES/", 0) == 0 || path.rfind("/SOUNDS/", 0) == 0 ||
         path.rfind("/SCRIPTS/", 0) == 0;
}

enum class AssetKind { Unknown, Icon, Melody, Palette, Sound };

inline AssetKind kindFor(const std::string& path) {
  if (path.rfind("/ICONS/", 0) == 0) return AssetKind::Icon;
  if (path.rfind("/MELODIES/", 0) == 0) return AssetKind::Melody;
  if (path.rfind("/PALETTES/", 0) == 0) return AssetKind::Palette;
  if (path.rfind("/SOUNDS/", 0) == 0) return AssetKind::Sound;
  return AssetKind::Unknown;
}

inline bool looksLikeImage(const unsigned char* data, unsigned n) {
  if (n >= 4 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8') return true;
  if (n >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return true;
  return n >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G';
}

// ID3v2 tag or an MPEG frame sync. Cheap by design: whether the frames are
// actually MPEG-1 Layer III only comes out when the decoder runs.
inline bool looksLikeMp3(const unsigned char* data, unsigned n) {
  if (n >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') return true;
  return n >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0;
}

inline bool contentLooksValid(AssetKind kind, const unsigned char* data, unsigned n) {
  switch (kind) {
    case AssetKind::Icon:
      if (n >= 4 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8')
        return true;
      return n >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
    case AssetKind::Melody:
      return n != 0 && rtttl::parse(std::string(reinterpret_cast<const char*>(data), n)).ok;
    case AssetKind::Palette: {
      if (n == 0) return false;
      if (looksLikeImage(data, n)) return false;
      for (unsigned i = 0; i < n; ++i) {
        const unsigned char c = data[i];
        if (c == '\t' || c == '\n' || c == '\r') continue;
        if (c < 0x20 || c == 0x7F) return false;
      }
      return true;
    }
    case AssetKind::Sound:
      return looksLikeMp3(data, n);
    case AssetKind::Unknown:
      return false;
  }
  return false;
}

inline const char* acceptedFormats(AssetKind kind) {
  switch (kind) {
    case AssetKind::Icon: return "GIF or JPEG";
    case AssetKind::Melody: return "RTTTL text";
    case AssetKind::Palette: return "text, one RRGGBB per line";
    case AssetKind::Sound: return "MP3 (MPEG-1 Layer III)";
    default: return "";
  }
}

}
}
