#pragma once

#include <string>

namespace awtrix {
namespace radio {


enum class PlaylistKind { None, M3u, Pls };

PlaylistKind kindFromUrl(const std::string& url);

bool parsePlaylist(const std::string& body, std::string& streamUrl);

}
}
