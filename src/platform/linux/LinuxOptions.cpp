#include "platform/linux/LinuxOptions.h"

#include <cstdio>

#include "platform/posix/Text.h"
#include "platform/tc002/contract/RuntimeContract.h"

namespace awtrix {
namespace {

int number(const char* value, int low, int high) {
  int out = -1;
  posix::parseInteger(value, low, high, out);
  return out;
}

void usage() {
  std::printf("awtrix-linux --data DIRECTORY [--port 8080] [--webui FILE] [--width 52] [--height 16]\n"
              "  [--board headless|tc002] (default: headless; TC002 requires 52x16)\n"
              "  [%s %s] (TC002 only: button and knob devices from awtrix-tc002d)\n"
              "  [%s %d] (awtrix-tc002d SOCK_SEQPACKET channel; its close stops the process)\n"
              "  [%s %d] (TC002 only: socket to the awtrix-tc002-audio-pcm speaker helper)\n"
              "  [--speech-voice FILE] (with the speaker: the ATTS voice that reads speech aloud; a file\n"
              "   that cannot be read leaves the clock without speech)\n"
              "  [%s poweron|software|panic|watchdog] (supervised only: why this start happens)\n"
              "  [%s 12-HEX-DIGITS] (supervised only: the device id; else a random one, kept in DATA/identity\n"
              "   when unsupervised)\n",
              tc002::kInputFdsFlag, tc002::inputFdsValue().c_str(), tc002::kSupervisorFdFlag, tc002::kSupervisorFd,
              tc002::kAudioFdFlag, tc002::kAudioFd, tc002::kStartReasonFlag, tc002::kUidFlag);
  std::puts("  [--performance-report NEW_FILE] (private aggregate JSON written at exit)\n"
            "  [--boot-intro [--boot-sound FILE]] (power-on intro before the first app; the MP3 FILE\n"
            "   plays with it on the TC002 speaker)\n"
            "  [--ca-file FILE] (PEM roots every HTTPS client verifies against, read once)\n"
            "  [--update-state FILE --update-dir DIRECTORY\n"
            "   [--release-root DIRECTORY]] (supervised only: POST /update\n"
            "   takes awtrix-ng-tc002.awup packages, stages them in the update directory and\n"
            "   hands them to the supervisor)\n"
            "  A --webui FILE ending in .gz is served gzip-encoded.\n"
            "Default: local development HTTP on loopback (ports 1024-65535).\n"
            "Device LAN service as on ESP32, with the login configured in the web UI:\n"
            "  --lan [--listen IPv4_ADDRESS] (default 0.0.0.0; --port 1-65535)\n"
            "Hardened administration:\n"
            "  --hardened --credentials-file FILE --tls-cert FILE --tls-key FILE\n"
            "  --origin https://HOST:PORT [--listen IPv4_ADDRESS] [--mqtt-ca-file FILE]\n"
            "Hardened MQTT requires an explicit CA file and broker credentials.\n"
            "SIGINT/SIGTERM flush application state and stop the process.");
}

bool setOnce(std::string& target, const char* value) {
  if (!value[0] || !target.empty()) return false;
  target = value;
  return true;
}

// Reads one option with its value: false for an unknown option or a refused value.
bool readValue(const std::string& arg, const char* value, LinuxOptions& o) {
  if (arg == "--data") o.data = value;
  else if (arg == "--board") o.boardType = value;
  else if (arg == "--boot-sound") return setOnce(o.bootSound, value);
  else if (arg == "--performance-report") return setOnce(o.performancePath, value);
  else if (arg == tc002::kInputFdsFlag) {
    if (value != tc002::inputFdsValue()) return false;
    o.physicalInput = true;
  }
  else if (arg == tc002::kSupervisorFdFlag) {
    if (o.supervised || value != std::to_string(tc002::kSupervisorFd)) return false;
    o.supervised = true;
  }
  else if (arg == tc002::kAudioFdFlag) {
    if (o.speakerRequested || value != std::to_string(tc002::kAudioFd)) return false;
    o.speakerRequested = true;
  }
  else if (arg == "--webui") o.webui = value;
  else if (arg == "--port") o.port = number(value, 1, 65535);
  else if (arg == "--width") o.width = number(value, 8, 128);
  else if (arg == "--height") o.height = number(value, 8, 32);
  else if (arg == "--credentials-file") o.administration.credentialsFile = value;
  else if (arg == "--tls-cert") o.administration.certificateFile = value;
  else if (arg == "--tls-key") o.administration.privateKeyFile = value;
  else if (arg == "--origin") o.administration.origin = value;
  else if (arg == "--listen") o.administration.listenAddress = value;
  else if (arg == "--mqtt-ca-file") o.administration.mqttCaFile = value;
  else if (arg == "--ca-file") return setOnce(o.caFile, value);
  else if (arg == "--speech-voice") return setOnce(o.speechVoice, value);
  else if (arg == "--update-state") return setOnce(o.updateOptions.statePath, value);
  else if (arg == "--release-root") return setOnce(o.updateOptions.releaseRoot, value);
  else if (arg == tc002::kUpdateDirFlag) return setOnce(o.updateOptions.workDirectory, value);
  else if (arg == tc002::kStartReasonFlag) return tc002::validStartReason(value) && setOnce(o.startReason, value);
  else if (arg == tc002::kUidFlag) return tc002::validUid(value) && setOnce(o.uid, value);
  else return false;
  return true;
}

}

int parseLinuxOptions(int argc, char** argv, LinuxOptions& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help") { usage(); return 0; }
    if (arg == "--hardened") { o.administration.hardened = true; continue; }
    if (arg == "--lan") { o.administration.lan = true; continue; }
    if (arg == "--boot-intro") { o.bootIntro = true; continue; }
    if (arg == tc002::kBluetoothFlag) { o.bluetooth = true; continue; }
    if (i + 1 == argc || !readValue(arg, argv[++i], o)) { usage(); return 2; }
  }
  const bool webUpdate = o.webUpdate();
  if ((!webUpdate && (!o.updateOptions.releaseRoot.empty() || !o.updateOptions.workDirectory.empty())) ||
      (webUpdate && o.updateOptions.workDirectory.empty())) {
    usage(); return 2;
  }
  if (webUpdate && !o.supervised) {
    std::fputs("--update-state requires --supervisor-fd\n", stderr); return 2;
  }
  if (!o.startReason.empty() && !o.supervised) {
    std::fprintf(stderr, "%s requires --supervisor-fd\n", tc002::kStartReasonFlag); return 2;
  }
  if (!o.uid.empty() && !o.supervised) {
    std::fprintf(stderr, "%s requires --supervisor-fd\n", tc002::kUidFlag); return 2;
  }
  if (o.bluetooth && !o.supervised) {
    std::fprintf(stderr, "%s requires --supervisor-fd\n", tc002::kBluetoothFlag); return 2;
  }
  if (!o.administration.lan && o.port > 0 && o.port < 1024) o.port = -1;
  if (o.data.empty() || o.port < 0 || o.width < 0 || o.height < 0) { usage(); return 2; }
  if (o.boardType != "headless" && o.boardType != "tc002") {
    std::fputs("Board must be headless or tc002\n", stderr); return 2;
  }
  if (!o.bootSound.empty() && !o.bootIntro) {
    std::fputs("--boot-sound requires --boot-intro\n", stderr); return 2;
  }
  if (!o.speechVoice.empty() && !o.speakerRequested) {
    std::fprintf(stderr, "--speech-voice requires %s\n", tc002::kAudioFdFlag); return 2;
  }
  return -1;
}

}
