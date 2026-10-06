#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

#include "platform/tc002/contract/tc002_layout.h"

namespace awtrix {
namespace tc002d {

// --boot-intro: "first" shows the power-on intro on the runtime's first start after power-on,
// "always" on every start, "never" not at all.
enum class BootIntro { First, Always, Never };
enum class IntroStarts { None, First, Every };

// The speaker: the awtrix_pcm module and its helper. An empty helper path means no speaker.
struct PcmBackendPaths {
  std::string helper;
  std::string module;
  std::string modules = "/proc/modules";
};

struct DaemonOptions {
  std::string root;
  std::string data;
  std::string runDir = TC002_VOLATILE_DIR TC002_DAEMON_RUN;
  std::string sysRoot;
  std::string uart = TC002_MCU_UART;
  int httpPort = 80;
  BootIntro bootIntro = BootIntro::First;
  std::string updateDir = TC002_VOLATILE_DIR TC002_UPDATE_WORK;

  std::string controlPath() const { return runDir + "/control.sock"; }
  std::string lockPath() const { return runDir + "/lock"; }
  std::string logPath() const { return runDir + "/daemon.log"; }
  // Written once a daemon ran in this boot (runDir lies in RAM).
  std::string startedPath() const { return runDir + "/started"; }
  // Written before this daemon reboots the clock itself.
  std::string rebootMarkerPath() const { return data + TC002_REBOOT_MARKER; }
  std::string deviceIdPath() const { return data + TC002_DEVICE_ID; }
  std::string keepAdbTcpFlagPath() const { return data + "/state/keep-adb-tcp"; }
  std::string autostartPath() const { return data + "/state/autostart"; }
  std::string staticAddressPath() const { return data + "/state/static-address"; }
  std::string runtimePath() const { return root + "/bin/awtrix-linux"; }
  std::string supplicantPath() const { return root + "/bin/wpa_supplicant"; }
  std::string udhcpcPath() const { return root + "/bin/udhcpc"; }
  std::string dhcpCallbackPath() const { return root + "/bin/dhcp-callback"; }
  // Writes the release slot for a web update; without it the web update is off.
  std::string flashHelperPath() const { return root + "/bin/awtrix-tc002-flash"; }
  // aic8800_bsp.ko, aic8800_fdrv.ko and awtrix_pcm.ko.
  std::string moduleDirectory() const { return root + "/lib/modules"; }
  PcmBackendPaths pcmBackend() const;
  // The release's programs and kernel modules that are missing or unusable. The daemon runs no
  // other ones, so it does not start while one is missing.
  std::vector<std::string> missingComponents() const;
  std::string bootSoundPath() const { return root + "/share/boot.mp3"; }
  // The voice that reads speech aloud; a release may come without one.
  std::string speechVoicePath() const { return root + "/share/speech/voice.atts"; }
  // share/index.html.gz, or share/index.html in a release without the compressed copy.
  std::string webIndexPath() const;
  std::string caFilePath() const { return root + "/share/ca-certificates.crt"; }
  std::string introShownPath() const { return sysRoot + "/tmp/awtrix-boot-intro.shown"; }
  // The loader's start counters of the release, the durable one and the one of this boot.
  std::vector<std::string> bootAttemptsPaths() const;
  IntroStarts introStarts() const;
};

struct CtlOptions {
  std::string runDir = TC002_VOLATILE_DIR TC002_DAEMON_RUN;
  std::string command;
  std::string payload;
  bool payloadFromStdin = false;
  int timeoutMs = 10000;

  std::string controlPath() const { return runDir + "/control.sock"; }
};

const char* bootIntroName(BootIntro intro);
const char* usageText();
bool parseDaemonOptions(int argc, const char* const* argv, DaemonOptions& out, std::string& error);
// Developer switches are regular files owned by root in the state directory; a symlink or a file
// someone else could have created does not count.
bool developerFlagPresent(const std::string& path, uid_t owner = 0);
bool regularFile(const std::string& path);
// /tmp is a RAM filesystem, so the marker lives exactly until the next power-on: a daemon started
// again (deploy, a zkswe restart) finds it and shows no second intro.
IntroStarts claimPowerOnIntro(IntroStarts starts, const std::string& marker);
// argv[0] is "ctl".
bool parseCtlOptions(int argc, const char* const* argv, CtlOptions& out, std::string& error);

}
}
