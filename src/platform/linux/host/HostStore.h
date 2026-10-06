#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <filesystem>

namespace awtrix {
namespace host {

// The host has no flash partition to report a size for, so everything that answers "how much
// room is there" measures the data directory against this stand-in for the device's storage area.
constexpr uint64_t kFsTotalBytes = 8u * 1024u * 1024u;

// Configure the data directory before starting worker threads.
void setDataDir(const std::string& dir);
const std::string& dataDir();

// Component-wise containment of already normalized/canonical paths (not a security check alone).
bool contains(const std::filesystem::path& root, const std::filesystem::path& candidate);

// Maps a device path such as "/ICONS/foo.jpg" onto the host data directory. Everything that would
// touch flash on the device goes through here. Invalid/escaping paths return an empty string.
std::string hostPath(const std::string& devicePath);

// Data operations reject traversal and paths escaping that directory, including existing symlinks.
bool readFile(const std::string& hostPath, std::string& out,
              std::size_t maxBytes = kFsTotalBytes);
bool writeFile(const std::string& hostPath, const std::string& data);

// Uploads leave this much of the data directory's filesystem free for settings and system state,
// which may still use it. Zero, the default, limits uploads by the quota alone. Set at startup.
void setUploadReserve(uint64_t bytes);
uint64_t uploadReserve();
// What script state may use: the filesystem's free space above a quarter of the upload reserve,
// or the quota when no reserve is set.
uint64_t stateRoom();
// writeFile for data a client uploads.
bool writeUpload(const std::string& hostPath, const std::string& data);
// The quota, or less when the filesystem above the upload reserve cannot hold that much.
uint64_t storageCapacity(uint64_t usedBytes);

// Only for an explicitly configured host asset (e.g. --webui), never an API path.
bool readTrustedFile(const std::string& path, std::string& out,
                     std::size_t maxBytes = kFsTotalBytes);

}
}
