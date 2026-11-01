#include "platform/tc002/voice/WavWriter.h"

#include <fcntl.h>
#include <unistd.h>

#include "platform/posix/Files.h"

namespace awtrix::tc002::voice {
namespace {

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "WAV samples are written in the host's byte order");

constexpr uint32_t kRiffHeadBytes = 8, kFormatBytes = 16;
constexpr uint16_t kPcmFormat = 1, kChannels = 1, kSampleBytes = sizeof(int16_t), kSampleBits = 8 * kSampleBytes;

void put16(std::string& out, uint16_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>(value >> 8));
}

void put32(std::string& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((value >> shift) & 0xff));
}

}

std::string wavHeader(uint32_t samples, uint32_t rate) {
  const uint32_t data = samples * kSampleBytes;
  std::string out = "RIFF";
  put32(out, kWavHeaderBytes - kRiffHeadBytes + data);
  out += "WAVEfmt ";
  put32(out, kFormatBytes);
  put16(out, kPcmFormat);
  put16(out, kChannels);
  put32(out, rate);
  put32(out, rate * kSampleBytes);
  put16(out, kSampleBytes);
  put16(out, kSampleBits);
  out += "data";
  put32(out, data);
  return out;
}

WavWriter::~WavWriter() { close(); }

bool WavWriter::open(const std::string& path, uint32_t rate) {
  close();
  rate_ = rate;
  samples_ = 0;
  failed_ = false;
  file_.reset(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (!file_.valid()) return false;
  const std::string header = wavHeader(0, rate_);
  if (posix::pwriteAll(file_.get(), 0, header.data(), header.size())) return true;
  file_.reset();
  return false;
}

bool WavWriter::write(const int16_t* samples, std::size_t count) {
  if (!file_.valid() || failed_) return false;
  failed_ = !posix::pwriteAll(file_.get(), wavBytes(samples_), samples, count * kSampleBytes);
  if (!failed_) samples_ += static_cast<uint32_t>(count);
  return !failed_;
}

bool WavWriter::close() {
  if (!file_.valid()) return true;
  if (!failed_) {
    const std::string header = wavHeader(samples_, rate_);
    failed_ = !posix::pwriteAll(file_.get(), 0, header.data(), header.size());
  }
  if (::close(file_.release()) != 0) failed_ = true;
  return !failed_;
}

}
