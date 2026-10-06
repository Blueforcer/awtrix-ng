#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// manifest.json of a TC002 release (tools/tc002/install/bundle.py): the release name, its counter
// and the files, at the root of the release image.
namespace awtrix {
namespace tc002 {
namespace update {

constexpr std::size_t kMaxReleaseManifestBytes = 256 * 1024;

struct RunningRelease {
  std::string release;
  std::uint64_t counter = 0;
};

RunningRelease readRunningRelease(const std::string& root);

// manifest.json of a release root; false when absent, too large or unreadable.
bool readReleaseManifestFile(const std::string& path, std::string& text);

}
}
}
