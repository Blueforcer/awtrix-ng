#include "platform/posix/Bytes.h"
#include "platform/tc002/audio/Tc002AudioSources.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace awtrix {
namespace tc002 {

namespace {

struct WavFormat {
  uint32_t rate = 0;
  uint8_t channels = 0;
  uint32_t dataBytes = 0;
};

// Walks the chunks up to the data chunk, which read and skip leave the caller in front of. read
// fills exactly n bytes and skip passes n; both answer false where the bytes end.
template <typename Read, typename Skip>
bool readWavHeader(Read read, Skip skip, WavFormat& out) {
  unsigned char header[16]{};
  if (!read(header, 12) || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) return false;
  const uint32_t riff = posix::le32(header + 4);
  if (riff < 4 || riff > 2 * 1024 * 1024) return false;
  uint32_t left = riff - 4;
  bool format = false;
  while (left >= 8) {
    if (!read(header, 8)) return false;
    left -= 8;
    const uint32_t size = posix::le32(header + 4);
    if (size > left || size + (size & 1) > left) return false;
    if (!std::memcmp(header, "fmt ", 4)) {
      if (format || size < 16 || !read(header, 16)) return false;
      const uint32_t channels = posix::le16(header + 2), rate = posix::le32(header + 4);
      if (posix::le16(header) != 1 || channels < 1 || channels > 2 || posix::le16(header + 14) != 16 ||
          posix::le16(header + 12) != channels * 2 || posix::le32(header + 8) != rate * channels * 2 ||
          (rate != 16000 && rate != 22050 && rate != 24000 && rate != 32000 && rate != 44100 && rate != 48000)) return false;
      out.rate = rate; out.channels = static_cast<uint8_t>(channels); format = true;
      if (!skip(size - 16 + (size & 1))) return false;
    } else if (!std::memcmp(header, "data", 4)) {
      if (!format || !size || size % (out.channels * 2)) return false;
      out.dataBytes = size;
      return true;
    } else if (!skip(size + (size & 1))) return false;
    left -= size + (size & 1);
  }
  return false;
}

// The format and where its samples start; false unless every sample is inside the bytes.
bool wavInMemory(const std::string& bytes, WavFormat& format, std::size_t& at) {
  at = 0;
  const auto read = [&](unsigned char* out, std::size_t n) {
    if (n > bytes.size() - at) return false;
    std::memcpy(out, bytes.data() + at, n);
    at += n;
    return true;
  };
  const auto skip = [&](std::size_t n) {
    if (n > bytes.size() - at) return false;
    at += n;
    return true;
  };
  return readWavHeader(read, skip, format) && format.dataBytes <= bytes.size() - at;
}

void toSamples(const unsigned char* bytes, std::size_t size, int16_t* out) {
  for (std::size_t i = 0; i < size / 2; ++i) {
    const int value = bytes[2 * i] | (int(bytes[2 * i + 1]) << 8);
    out[i] = static_cast<int16_t>(value < 32768 ? value : value - 65536);
  }
}

// ID3v2 tags ahead of the audio, each with its optional footer.
std::size_t skipId3(const std::string& bytes) {
  std::size_t at = 0;
  while (bytes.size() - at >= 10 && !bytes.compare(at, 3, "ID3")) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data() + at);
    if ((p[6] | p[7] | p[8] | p[9]) & 0x80) break;
    const std::size_t size = (std::size_t(p[6]) << 21) | (std::size_t(p[7]) << 14) |
                             (std::size_t(p[8]) << 7) | p[9];
    const std::size_t total = 10 + size + ((p[5] & 0x10) ? 10 : 0);
    if (total > bytes.size() - at) break;
    at += total;
  }
  return at;
}

// Junk this long between the tag and the first frame is no MP3 any more.
constexpr std::size_t kMp3SyncWindow = 64 * 1024;

// The first frame whose successor starts where its length says, or that ends the bytes.
bool findMp3(const std::string& bytes, std::size_t& at) {
  const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
  const std::size_t start = skipId3(bytes);
  const std::size_t stop = std::min(bytes.size(), start + kMp3SyncWindow);
  std::size_t from = start;
  std::size_t found = 0;
  while (mp3::findSync(data, stop, from, found)) {
    from = found + 1;
    mp3::FrameHeader first, second;
    if (!mp3::parseHeader(data + found, bytes.size() - found, first) || !mp3::isSupported(first)) continue;
    const int length = first.frameBytes();
    if (length <= 4) continue;
    const std::size_t next = found + static_cast<std::size_t>(length);
    const bool last = next == bytes.size();
    if (!last && (next > bytes.size() || !mp3::parseHeader(data + next, bytes.size() - next, second) ||
                  second.version != first.version || second.sampleRateHz != first.sampleRateHz))
      continue;
    at = found;
    return true;
  }
  return false;
}

}

WavFileSource::WavFileSource(const std::string& path) {
  file_ = std::fopen(path.c_str(), "rb");
  if (!file_) return;
  std::FILE* file = file_;
  WavFormat format;
  valid_ = readWavHeader([file](unsigned char* out, std::size_t n) { return std::fread(out, 1, n, file) == n; },
                         [file](std::size_t n) { return !std::fseek(file, static_cast<long>(n), SEEK_CUR); },
                         format);
  rate_ = format.rate; channels_ = format.channels; remaining_ = format.dataBytes;
}
WavFileSource::~WavFileSource() { if (file_) std::fclose(file_); }
PcmSource::Read WavFileSource::next(const int16_t*& samples, std::size_t& frames) {
  frames = 0; samples = nullptr;
  if (!valid_) return Read::Error;
  if (!remaining_) return Read::End;
  unsigned char bytes[sizeof(block_)];
  const std::size_t size = std::min<std::size_t>(remaining_, sizeof(bytes));
  if (std::fread(bytes, 1, size, file_) != size) { valid_ = false; return Read::Error; }
  toSamples(bytes, size, block_);
  remaining_ -= static_cast<uint32_t>(size); samples = block_; frames = size / (channels_ * 2);
  return Read::Data;
}

WavMemorySource::WavMemorySource(std::shared_ptr<const std::string> bytes) : bytes_(std::move(bytes)) {
  WavFormat format;
  std::size_t at = 0;
  if (!bytes_ || !wavInMemory(*bytes_, format, at)) return;
  at_ = at;
  end_ = at + format.dataBytes;
  rate_ = format.rate;
  channels_ = format.channels;
}

PcmSource::Read WavMemorySource::next(const int16_t*& samples, std::size_t& frames) {
  frames = 0; samples = nullptr;
  if (!end_) return Read::Error;
  if (at_ >= end_) return Read::End;
  const std::size_t size = std::min(end_ - at_, sizeof(block_));
  toSamples(reinterpret_cast<const unsigned char*>(bytes_->data() + at_), size, block_);
  at_ += size; samples = block_; frames = size / (channels_ * 2);
  return Read::Data;
}

bool checkClip(const std::string& bytes, std::string& error) {
  if (!bytes.compare(0, 4, "RIFF")) {
    WavFormat format;
    std::size_t at = 0;
    if (wavInMemory(bytes, format, at)) return true;
    error = "unsupported WAV";
    return false;
  }
  std::size_t at = 0;
  if (findMp3(bytes, at)) return true;
  error = "not WAV or MP3";
  return false;
}

std::unique_ptr<PcmSource> openClip(std::shared_ptr<const std::string> bytes) {
  if (!bytes) return nullptr;
  if (!bytes->compare(0, 4, "RIFF")) {
    std::unique_ptr<WavMemorySource> wav(new WavMemorySource(std::move(bytes)));
    if (wav->opened()) return wav;
    return nullptr;
  }
  std::unique_ptr<Mp3MemorySource> mp3(new Mp3MemorySource(std::move(bytes)));
  if (mp3->opened()) return mp3;
  return nullptr;
}

namespace {

constexpr std::size_t kSineBits = 10;

const std::array<int16_t, 1u << kSineBits>& sineTable() {
  static const std::array<int16_t, 1u << kSineBits> table = [] {
    std::array<int16_t, 1u << kSineBits> t{};
    for (std::size_t i = 0; i < t.size(); ++i)
      t[i] = static_cast<int16_t>(std::lround(32767.0 * std::sin(6.283185307179586 * i / t.size())));
    return t;
  }();
  return table;
}

int readFile(void* context, uint8_t* out, std::size_t max) {
  return static_cast<int>(std::fread(out, 1, max, static_cast<std::FILE*>(context)));
}

}

ToneSource::ToneSource(std::vector<rtttl::Note> notes, uint16_t timeUnit)
    : notes_(std::move(notes)), timeUnit_(timeUnit), block_(kBlockFrames) {}

bool ToneSource::startNote() {
  for (; index_ < notes_.size(); ++index_) {
    const rtttl::Note& note = notes_[index_];
    const uint64_t frames =
        static_cast<uint64_t>(rtttl::noteMs(note.duration, timeUnit_)) * kRate / 1000u;
    if (!frames) continue;
    noteFrames_ = static_cast<uint32_t>(frames);
    position_ = 0;
    phase_ = 0;
    step_ = note.frequency && note.frequency < kRate / 2
                ? static_cast<uint32_t>((static_cast<uint64_t>(note.frequency) << 32) / kRate)
                : 0;
    return true;
  }
  return false;
}

PcmSource::Read ToneSource::next(const int16_t*& samples, std::size_t& frames) {
  const auto& sine = sineTable();
  if (!started_) {
    started_ = true;
    if (!startNote()) return Read::End;
  }
  std::size_t count = 0;
  while (count < kBlockFrames && index_ < notes_.size()) {
    if (position_ >= noteFrames_) {
      ++index_;
      if (!startNote()) break;
      continue;
    }
    int32_t value = 0;
    if (step_) {
      const uint32_t fade = std::min<uint32_t>(kFadeFrames, noteFrames_ / 2 ? noteFrames_ / 2 : 1);
      const uint32_t fromEnd = noteFrames_ - 1 - position_;
      const uint32_t envelope = std::min(std::min(position_, fromEnd), fade);
      value = static_cast<int32_t>(sine[phase_ >> (32 - kSineBits)]) * kAmplitude / 32767;
      value = value * static_cast<int32_t>(envelope) / static_cast<int32_t>(fade);
      phase_ += step_;
    }
    block_[count++] = static_cast<int16_t>(value);
    ++position_;
  }
  if (!count) return Read::End;
  samples = block_.data();
  frames = count;
  return Read::Data;
}

Mp3Source::Mp3Source() : decoder_(new mp3::Decoder()), pcm_(mp3::kMaxPcmPerFrame) {
  walk_.reset(new mp3::Mp3FileDecoder(*decoder_));
}

Mp3Source::~Mp3Source() = default;

int Mp3Source::pull(void* self, uint8_t* out, std::size_t max) {
  return static_cast<Mp3Source*>(self)->read(out, max);
}

Mp3FileSource::Mp3FileSource(const std::string& hostPath) : file_(std::fopen(hostPath.c_str(), "rb")) {}

Mp3FileSource::~Mp3FileSource() {
  if (file_) std::fclose(file_);
}

int Mp3FileSource::read(uint8_t* out, std::size_t max) {
  return file_ ? readFile(file_, out, max) : -1;
}

Mp3MemorySource::Mp3MemorySource(std::shared_ptr<const std::string> bytes) : bytes_(std::move(bytes)) {
  opened_ = bytes_ && findMp3(*bytes_, at_);
}

int Mp3MemorySource::read(uint8_t* out, std::size_t max) {
  if (!opened_) return -1;
  const std::size_t size = std::min(max, bytes_->size() - at_);
  std::memcpy(out, bytes_->data() + at_, size);
  at_ += size;
  return static_cast<int>(size);
}

PcmSource::Read Mp3Source::next(const int16_t*& samples, std::size_t& frames) {
  mp3::DecodeResult result;
  switch (walk_->next(pull, this, pcm_.data(), result)) {
    case mp3::Mp3FileDecoder::Step::Done: return Read::End;
    case mp3::Mp3FileDecoder::Step::Error: return Read::Error;
    case mp3::Mp3FileDecoder::Step::Frame: break;
  }
  if (result.samples <= 0 || result.channels < 1 || result.channels > 2) return Read::Error;
  rate_ = static_cast<uint32_t>(result.sampleRateHz);
  channels_ = static_cast<uint8_t>(result.channels);
  samples = pcm_.data();
  frames = static_cast<std::size_t>(result.samples);
  return Read::Data;
}

}
}
