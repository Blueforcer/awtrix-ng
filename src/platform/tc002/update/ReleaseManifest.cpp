#include "platform/tc002/update/ReleaseManifest.h"

#include <fcntl.h>

#include "core/api/JsonReader.h"
#include "platform/posix/Files.h"
#include "platform/tc002/contract/ReleaseName.h"

namespace awtrix::tc002::update {

bool readReleaseManifestFile(const std::string& path, std::string& text) {
  text.clear();
  return posix::readRegularFile(AT_FDCWD, path, kMaxReleaseManifestBytes, text);
}

RunningRelease readRunningRelease(const std::string& root) {
  RunningRelease running;
  std::string text;
  if (root.empty() || !readReleaseManifestFile(root + "/manifest.json", text) || !api::isWellFormed(text))
    return running;
  const api::JsonReader manifest(text);
  running.release = api::memberText(manifest, "release");
  if (!validReleaseName(running.release)) running.release.clear();
  api::memberValue(manifest, "counter").asUnsigned(running.counter);
  return running;
}

}
