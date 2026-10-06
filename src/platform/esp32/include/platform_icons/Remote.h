#pragma once

namespace awtrix {

struct RemoteIconEntry {
  static void resetRemote() {}
  static constexpr int offsetX() { return 0; }
  static constexpr int offsetY() { return 0; }
};

class RemoteIconSource {
 public:
  static constexpr bool remoteChanged(const RemoteIconEntry&) { return false; }
};

}
