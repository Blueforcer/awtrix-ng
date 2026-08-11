#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
namespace sim {

// The host has no flash partition to report a size for, so everything that answers "how much
// room is there" measures the data directory against this stand-in for the device's storage area.
constexpr uint64_t kFsTotalBytes = 8u * 1024u * 1024u;

void setDataDir(const std::string& dir);
const std::string& dataDir();

// Maps a device path such as "/ICONS/foo.jpg" onto the host data directory. Everything that would
// touch flash on the device goes through here.
std::string hostPath(const std::string& devicePath);

bool readFile(const std::string& hostPath, std::string& out);
bool writeFile(const std::string& hostPath, const std::string& data);

}
}
