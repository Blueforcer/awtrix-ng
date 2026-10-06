#pragma once

#include <cstdint>
#include <string>

#include "platform/tc002/update/PackageFormat.h"

// The staged package as the runtime verified it (PackageVerifier.h): the daemon re-reads the
// structure and checks the manifest hash; the flash helper hashes the payload before it writes.
namespace awtrix {
namespace tc002d {

using tc002::update::PackageHeader;
constexpr uint64_t kMaxPackagePayloadBytes = 64ULL << 20;

// fd must be at offset 0 of a regular file whose size is fileSize.
bool readPackageHeader(int fd, uint64_t fileSize, PackageHeader& out, std::string& error);

}
}
