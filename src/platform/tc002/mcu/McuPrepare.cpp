// Runs outside the supervisor event loop. Never opens the MCU UART or flashes.
#include "platform/tc002/daemon/McuFirmware.h"
#include "platform/tc002/mcu/McuPatch.h"
#include "platform/tc002/mcu/McuPrepare.h"
#include "platform/posix/Files.h"
#include "platform/linux/host/vendor/httplib.h"

#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>

using awtrix::tc002d::mcu::Firmware;
using awtrix::tc002d::mcu::firmwareHash;
using awtrix::tc002d::mcu::kBaseHash;
using awtrix::tc002d::mcu::readFirmwareFile;
namespace {
int error(int code, const char* reason) { std::fprintf(stderr,"MCU preparation: %s\n",reason); return code; }
}
int awtrix::tc002::mcu::prepare(int argc,char** argv) {
  if (argc<4 || argc>5 || (argc==5 && std::strcmp(argv[4],"--offline")))
    return error(2,"usage: awtrix-linux --prepare-mcu PATCH_DIR PRIVATE_CACHE CA_FILE [--offline]");
  ::alarm(45); // Also bounds DNS/TLS and CPU work; the supervisor enforces a second deadline.
  try {
    Firmware firmware;
    firmware.load(argv[1],"");
    if (!firmware.valid()) return error(13,firmware.status().c_str());
    const std::string cache=argv[2];
    if (!awtrix::posix::ensurePrivateDirectory(cache)) return error(14,"private cache unavailable");
    const std::string basePath=cache+"/base-v1.0.17.fot";
    std::string base;
    if (!readFirmwareFile(basePath,base,35840)) {
      if (errno!=ENOENT) return error(14,"cannot read cached original");
      if (argc==5) return error(10,"original is missing (offline mode)");
      httplib::SSLClient client("cdn.ulanzistudio.com",443);
      client.set_ca_cert_path(argv[3]);
      client.enable_server_certificate_verification(true);
      client.set_follow_location(false);
      client.set_connection_timeout(8); client.set_read_timeout(8); client.set_write_timeout(8);
      client.set_decompress(false);
      httplib::Request request;
      request.method="GET";
      request.path="/UClock/Ulanzi_mcu_total_V1.0.17_real.fot";
      request.headers.emplace("Accept-Encoding","identity");
      request.content_receiver=[&](const char* bytes,std::size_t n,uint64_t,uint64_t) {
        if (n>35840-base.size()) return false;
        base.append(bytes,n); return true;
      };
      const auto response=client.send(request);
      if (!response || response->status!=200) return error(11,"manufacturer HTTPS download failed; check network and clock time");
      if (base.size()!=35840 || firmwareHash(base)!=kBaseHash) return error(12,"downloaded original SHA-256 mismatch");
      if (!awtrix::posix::replaceText(basePath,base)) return error(14,"cannot retain verified original");
    }
    if (base.size()!=35840 || firmwareHash(base)!=kBaseHash) return error(12,"cached original SHA-256 mismatch");
    const auto image=assembleFirmware(base,firmware.extension());
    if (!firmware.acceptPrepared(image)) return error(13,firmware.status().c_str());
    if (!awtrix::posix::replaceText(cache+"/prepared.pot",image)) return error(14,"cannot save verified local image");
    std::printf("MCU build %u prepared locally: %zu bytes, SHA-256 %s\n",firmware.target().version,image.size(),firmware.digest().c_str());
    return 0;
  } catch (const std::exception& e) { return error(13,e.what()); }
}
