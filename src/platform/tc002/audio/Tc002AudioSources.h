#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/Mp3Decoder.h"
#include "core/audio/Mp3FileDecoder.h"
#include "core/sound/Rtttl.h"

namespace awtrix {
namespace tc002 {

// Interleaved s16 PCM, pulled one block at a time by the audio thread. Wait means "nothing now,
// ask again later"; the format is only known once the first Data arrived. An endless source
// (a station) carries on after a lost generation instead of failing.
class PcmSource {
 public:
  enum class Read : uint8_t { Data, Wait, End, Error };
  virtual ~PcmSource() = default;
  virtual Read next(const int16_t*& samples, std::size_t& frames) = 0;
  virtual uint32_t rate() const = 0;
  virtual uint8_t channels() const = 0;
  virtual bool endless() const { return false; }
  // How far ahead of the speaker, in ms, the player may queue; 0 lets the helper's window decide.
  virtual uint32_t leadMs() const { return 0; }
};

// RTTTL notes as a sine at 44.1 kHz mono; each note fades in and out so the joins do not click.
class ToneSource final : public PcmSource {
 public:
  static constexpr uint32_t kRate = 44100;
  static constexpr int16_t kAmplitude = 6350;
  static constexpr std::size_t kBlockFrames = 1024;
  static constexpr uint32_t kFadeFrames = 176;

  ToneSource(std::vector<rtttl::Note> notes, uint16_t timeUnit);
  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return kRate; }
  uint8_t channels() const override { return 1; }

 private:
  bool startNote();

  std::vector<rtttl::Note> notes_;
  uint16_t timeUnit_;
  std::size_t index_ = 0;
  uint32_t noteFrames_ = 0;
  uint32_t position_ = 0;
  uint32_t phase_ = 0;
  uint32_t step_ = 0;
  bool started_ = false;
  std::vector<int16_t> block_;
};

// MP3 frames decoded one per block; read() supplies the encoded bytes and answers <= 0 at the end.
class Mp3Source : public PcmSource {
 public:
  ~Mp3Source() override;
  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return rate_; }
  uint8_t channels() const override { return channels_; }

 protected:
  Mp3Source();
  virtual int read(uint8_t* out, std::size_t max) = 0;

 private:
  static int pull(void* self, uint8_t* out, std::size_t max);

  std::unique_ptr<mp3::Decoder> decoder_;
  std::unique_ptr<mp3::Mp3FileDecoder> walk_;
  std::vector<int16_t> pcm_;
  uint32_t rate_ = 0;
  uint8_t channels_ = 0;
};

class Mp3FileSource final : public Mp3Source {
 public:
  explicit Mp3FileSource(const std::string& hostPath);
  ~Mp3FileSource() override;
  bool opened() const { return file_ != nullptr; }

 private:
  int read(uint8_t* out, std::size_t max) override;

  std::FILE* file_ = nullptr;
};

// A clip's MP3, from its first frame on: a tag in front is never decoded.
class Mp3MemorySource final : public Mp3Source {
 public:
  explicit Mp3MemorySource(std::shared_ptr<const std::string> bytes);
  bool opened() const { return opened_; }

 private:
  int read(uint8_t* out, std::size_t max) override;

  std::shared_ptr<const std::string> bytes_;
  std::size_t at_ = 0;
  bool opened_ = false;
};

// Bounded RIFF/WAVE PCM16 reader for Assist responses, independent of MP3.
class WavFileSource final : public PcmSource {
 public:
  explicit WavFileSource(const std::string& path);
  ~WavFileSource() override;
  bool opened() const { return valid_; }
  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return rate_; }
  uint8_t channels() const override { return channels_; }
 private:
  std::FILE* file_ = nullptr;
  bool valid_ = false;
  uint32_t remaining_ = 0, rate_ = 0;
  uint8_t channels_ = 0;
  int16_t block_[2048]{};
};

// The same WAV rules as WavFileSource, over a clip held in memory.
class WavMemorySource final : public PcmSource {
 public:
  explicit WavMemorySource(std::shared_ptr<const std::string> bytes);
  bool opened() const { return end_ != 0; }
  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return rate_; }
  uint8_t channels() const override { return channels_; }

 private:
  std::shared_ptr<const std::string> bytes_;
  std::size_t at_ = 0, end_ = 0;
  uint32_t rate_ = 0;
  uint8_t channels_ = 0;
  int16_t block_[2048]{};
};

// A clip is a whole recording sent in one piece: RIFF/WAVE PCM16 (mono or stereo at 16, 22.05,
// 24, 32, 44.1 or 48 kHz) or MPEG layer III, an ID3 tag allowed. checkClip() judges the bytes
// without decoding them and names what is wrong; openClip() plays bytes it accepted.
bool checkClip(const std::string& bytes, std::string& error);
std::unique_ptr<PcmSource> openClip(std::shared_ptr<const std::string> bytes);

}
}
