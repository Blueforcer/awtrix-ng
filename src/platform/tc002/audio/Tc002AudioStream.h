#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/Mp3Decoder.h"
#include "core/radio/StreamPolicy.h"
#include "platform/tc002/audio/Tc002AudioSources.h"

namespace awtrix {
namespace tc002 {

// An internet radio station. A detached network thread fills a compressed ring (it may sit in a
// connect or DNS lookup for seconds, so dropping the source never waits for it); the audio
// thread decodes from the ring on demand. Reconnects with the ESP32's back-off.
class StreamSource final : public PcmSource {
 public:
  static constexpr std::size_t kRingBytes = radio::kInputBufferBytes;
  static constexpr std::size_t kPrerollBytes = radio::kPrerollBytes;
  static constexpr std::size_t kUndecodableAfterBytes = radio::kUndecodableAfterBytes;

  explicit StreamSource(const std::string& url);
  ~StreamSource() override;
  StreamSource(const StreamSource&) = delete;
  StreamSource& operator=(const StreamSource&) = delete;

  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return rate_; }
  uint8_t channels() const override { return channels_; }
  bool endless() const override { return true; }

  bool takeTitle(std::string& out);
  bool takeError(std::string& out);
  bool connected() const;
  uint32_t bufferBytes() const { return buffered_; }
  uint32_t starvedMs() const { return starvedMs_; }
  uint32_t decodeUs() const { return decodeUs_; }

  // Network threads outlive their source until their blocking call returns; shutdown waits here.
  static bool waitForNetworkThreads(int timeoutMs);

  struct Shared;

 private:
  std::shared_ptr<Shared> shared_;
  std::unique_ptr<mp3::Decoder> decoder_;
  std::size_t bytesSeen_ = 0;
  uint32_t seenConnection_ = 0;
  uint32_t buffered_ = 0;
  bool prerolled_ = false;
  bool decodedAnything_ = false;
  bool fatal_ = false;
  std::vector<int16_t> pcm_;
  uint32_t rate_ = 0;
  uint8_t channels_ = 0;
  int64_t starvedSinceMs_ = -1;
  uint32_t starvedMs_ = 0;
  uint32_t decodeUs_ = 0;
};

}
}
