#include "platform/tc002/daemon/update/Package.h"

#include <utility>

namespace awtrix::tc002d {
bool readPackageHeader(int fd, uint64_t fileSize, PackageHeader& out, std::string& error) {
  tc002::update::CheckedManifest manifest;
  tc002::update::FormatProblem problem;
  if (!tc002::update::readCheckedManifest(fd, fileSize, kMaxPackagePayloadBytes, manifest, problem)) {
    out = PackageHeader{};
    error = problem.message;
    return false;
  }
  out = std::move(manifest.header);
  return true;
}
}
