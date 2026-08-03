#include "core/radio/PlaylistParser.h"

#include <algorithm>
#include <cctype>

namespace awtrix {
namespace radio {

namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string trim(const std::string& value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
  while (end > begin) {
    const char c = value[end - 1];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    --end;
  }
  return value.substr(begin, end - begin);
}

bool looksLikeUrl(const std::string& value) {
  const std::string lower = toLower(value);
  return lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0;
}

bool isPlaylistUrl(const std::string& value) { return kindFromUrl(value) != PlaylistKind::None; }

}

PlaylistKind kindFromUrl(const std::string& url) {
  const std::size_t query = url.find_first_of("?#");
  const std::string path = toLower(query == std::string::npos ? url : url.substr(0, query));
  if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".m3u") == 0) return PlaylistKind::M3u;
  if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".m3u8") == 0) return PlaylistKind::M3u;
  if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".pls") == 0) return PlaylistKind::Pls;
  return PlaylistKind::None;
}

// Pulls the first usable stream URL out of either format in one scan: M3U lines are bare URLs,
// PLS lines are "FileN=url". Comments, section headers and other PLS keys are ignored.
bool parsePlaylist(const std::string& body, std::string& streamUrl) {
  std::size_t position = 0;
  while (position < body.size()) {
    std::size_t end = body.find('\n', position);
    if (end == std::string::npos) end = body.size();
    const std::string line = trim(body.substr(position, end - position));
    position = end + 1;
    if (line.empty()) continue;

    std::string candidate = line;
    if (line[0] == '#' || line[0] == '[') continue;
    const std::size_t equals = line.find('=');
    if (equals != std::string::npos) {
      const std::string key = toLower(trim(line.substr(0, equals)));
      if (key.rfind("file", 0) != 0) continue;
      candidate = trim(line.substr(equals + 1));
    }

    if (!looksLikeUrl(candidate)) continue;
    // Skip entries that are themselves playlists; the caller only follows one level.
    if (isPlaylistUrl(candidate)) continue;
    streamUrl = candidate;
    return true;
  }
  return false;
}

}
}
