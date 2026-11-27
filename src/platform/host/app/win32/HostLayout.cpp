#include "platform/host/app/win32/HostLayout.h"

namespace awtrix::hostapp::win32 {

std::filesystem::path HostLayout::log() const { return data / "awtrix-host.log"; }

std::vector<std::string> HostLayout::hostArgs() const {
  return {(program / "awtrix-host.exe").u8string(),
          "--state",
          data.u8string(),
          "--log",
          log().u8string(),
          "--exit-with-stdin",
          "--binary",
          (program / "awtrix-script-runner.exe").u8string(),
          "--webui",
          (program / "webui" / "index.html").u8string(),
          "--ca-file",
          (program / "ca-certificates.crt").u8string()};
}

}
