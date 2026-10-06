#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "platform/tc002/daemon/McuFrame.h"
#include "platform/tc002/daemon/McuPcm.h"
#include "platform/tc002/daemon/McuStream.h"
#include "platform/tc002/daemon/McuFirmware.h"
#include "platform/tc002/daemon/McuUpgrade.h"
#include "platform/tc002/contract/tc002_layout.h"
#include "platform/tc002/daemon/Service.h"

// Continuous owner of the MCU UART (raw 1.5 Mbaud 8N1). Queries version once, USB every second
// and battery every ten seconds, one query in flight at a time. PCM requests temporarily own
// the same protocol channel. A power value not refreshed for staleMs becomes unknown.
// Acknowledgements and unknown commands are counted. The UART is reopened after errors.
namespace awtrix {
namespace tc002d {

struct McuOptions {
  std::string path = TC002_MCU_UART;
  int64_t usbIntervalMs = 1000;
  int64_t batteryIntervalMs = 10000;
  int64_t replyTimeoutMs = 500;
  int64_t staleMs = 30000;
  int64_t reopenDelayMs = 5000;
  int64_t versionRetryMs = 3000;
  unsigned versionAttempts = 3;
  std::string firmwareDirectory;
  std::string firmwareJournal;
  std::string firmwareCache;
  std::string firmwareHelper;
  std::vector<std::string> firmwareHelperArguments;
  std::string firmwareCaFile;
};

class McuService : public Service {
 public:
  McuService(DeviceState& state, McuOptions options);

  const char* name() const override { return "mcu"; }
  bool start(int64_t nowMs) override;
  void pollInterest(std::vector<PollInterest>& out) const override;
  void onReady(int fd, short revents, int64_t nowMs) override;
  int64_t nextDeadlineMs() const override;
  void onTime(int64_t nowMs) override;
  void requestStop(int64_t nowMs) override;
  bool onChildExit(pid_t pid, int status, int64_t nowMs) override;
  bool stopped() const override { return !uart_.valid() && preparePid_ < 0; }

  void appendStatus(api::JsonWriter& json, int64_t nowMs) const;
  static int batteryMillivolts(uint16_t word);

  bool open() const { return uart_.valid(); }
  const std::string& version() const { return version_; }
  bool firmwareBusy() const { return upgrading_ || verifyDeadline_ >= 0 || preparePid_ > 0; }
  bool busy() const { return firmwareBusy() || pcm_.active() || stream_.active() || pendingStream_; }
  void allowFirmwareUpdate(std::function<bool()> gate) { updateAllowed_ = std::move(gate); }
  bool pcmAvailable() const;
  // The callback runs on the service loop, after a validated end or a bounded failure.
  using PcmCallback = std::function<void(const mcu::PcmCapture&)>;
  bool requestPcm(unsigned halves, PcmCallback done, int64_t nowMs);
  bool streamAvailable() const;
  bool startStream(int epoch, mcu::Stream::Consumer consumer, int64_t nowMs);
  void controlStream(int epoch, bool stop, int64_t nowMs);

 private:
  bool openUart(int64_t nowMs);
  void closeUart(int64_t nowMs, const char* reason);
  void pump(int64_t nowMs);
  void handle(const mcu::Frame& frame, int64_t nowMs);
  void publish(int64_t nowMs);
  void firmwareProgress(int64_t nowMs);
  bool prepareFirmware(int64_t nowMs);
  void resetIdentity(int64_t nowMs);
  void flushPcm(int64_t nowMs);
  void finishPcm(int64_t nowMs);
  void finishStream(int64_t nowMs);
  void cancelPendingStream(const char* reason, int64_t nowMs);
  bool writeFrame(const uint8_t* bytes, std::size_t length, uint8_t command, int64_t nowMs);

  DeviceState& state_;
  McuOptions options_;
  posix::UniqueFd uart_;
  bool stopping_ = false;
  mcu::FrameParser parser_;
  mcu::Firmware firmware_;
  mcu::Upgrade upgrade_;
  mcu::PcmCapture pcm_;
  mcu::Stream stream_;
  int pendingStream_ = 0;
  int64_t pendingStreamAt_ = -1;
  mcu::Stream::Consumer pendingStreamConsumer_;
  PcmCallback pcmDone_;
  uint8_t pcmTx_[mcu::kPcmRequestBytes]{};
  std::size_t pcmSent_ = mcu::kPcmRequestBytes;
  uint64_t pcmCaptures_ = 0, pcmFailures_ = 0;
  std::string pcmError_;
  pid_t preparePid_ = -1;
  int64_t prepareDeadline_ = -1, prepareRetryAt_ = -1;
  bool prepared_ = false;
  std::string prepareStatus_;
  mcu::Identity identity_;
  std::function<bool()> updateAllowed_;
  bool identitySeen_ = false, identityMalformed_ = false, upgrading_ = false, upgradeHalted_ = false;
  unsigned identityTries_ = 0;
  int64_t identityAt_ = -1, verifyDeadline_ = -1, verifyStart_ = -1;

  int64_t reopenAt_ = -1;
  unsigned openFailures_ = 0;
  int lastOpenError_ = 0;
  unsigned opens_ = 0;
  bool termiosAdjusted_ = false;
  uint32_t observedCflag_ = 0;

  uint8_t outstanding_ = 0;
  int64_t outstandingDeadline_ = -1;
  int64_t versionAt_ = -1, usbAt_ = -1, batteryAt_ = -1;
  unsigned versionTries_ = 0;

  std::string version_;
  bool usbSeen_ = false, batterySeen_ = false;
  // Flash authorization needs a new sample after capture/preparation; the displayed power
  // measurement remains valid until the normal staleness deadline.
  bool usbRefreshRequired_ = true;
  uint8_t usbRaw_ = 0, batteryFirst_ = 0;
  uint16_t batteryWord_ = 0;
  int64_t usbSeenAt_ = 0, batterySeenAt_ = 0;

  uint64_t rxBytes_ = 0, frames_ = 0, acks_ = 0, unknown_ = 0, queries_ = 0, timeouts_ = 0, writeErrors_ = 0;
};

}
}
