#pragma once

#include <string>

namespace awtrix {
namespace sim {

void setDataDir(const std::string& dir);
const std::string& dataDir();

// Maps a device path such as "/ICONS/foo.jpg" onto the host data directory. Everything that would
// touch flash on the device goes through here.
std::string hostPath(const std::string& devicePath);

bool readFile(const std::string& hostPath, std::string& out);
bool writeFile(const std::string& hostPath, const std::string& data);

}
}
