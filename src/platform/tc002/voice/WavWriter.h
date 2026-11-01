#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/posix/UniqueFd.h"

namespace awtrix::tc002::voice {

constexpr std::size_t kWavHeaderBytes = 44;

constexpr std::size_t wavBytes(std::size_t samples) { return kWavHeaderBytes + samples * sizeof(int16_t); }

std::string wavHeader(uint32_t samples, uint32_t rate);

class WavWriter {
 public:
  WavWriter() = default;
  ~WavWriter();
  WavWriter(const WavWriter&) = delete;
  WavWriter& operator=(const WavWriter&) = delete;

  bool open(const std::string& path, uint32_t rate);
  bool write(const int16_t* samples, std::size_t count);
  bool close();
  uint32_t samples() const { return samples_; }

 private:
  posix::UniqueFd file_;
  uint32_t rate_ = 0;
  uint32_t samples_ = 0;
  bool failed_ = false;
};

}
