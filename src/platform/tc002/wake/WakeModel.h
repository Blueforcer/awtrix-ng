#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace awtrix::tc002::wake {

class WakeModel {
 public:
  static constexpr std::size_t kFeatures = 40;

  static std::unique_ptr<WakeModel> load(const uint8_t* data, std::size_t size, std::string& error);
  ~WakeModel();

  std::size_t frames() const { return frames_; }
  float inputScale() const { return inputScale_; }
  int32_t inputZeroPoint() const { return inputZeroPoint_; }
  int8_t* input() { return input_; }

  uint8_t invoke();
  const uint8_t* output() const { return output_; }
  std::size_t outputSize() const { return outputSize_; }
  bool outputIsUint8() const { return outputUint8_; }

  void reset();

  std::size_t arenaBytes() const { return arena_.size(); }

  struct Graph;

 private:
  WakeModel() = default;
  void run(std::size_t graph);

  std::vector<uint8_t> file_;
  std::vector<uint8_t> arena_;
  std::vector<Graph> graphs_;
  std::vector<uint8_t> called_;
  int8_t* input_ = nullptr;
  const uint8_t* output_ = nullptr;
  std::size_t outputSize_ = 0;
  bool outputUint8_ = false;
  std::size_t frames_ = 0;
  float inputScale_ = 0.0f;
  int32_t inputZeroPoint_ = 0;
};

}
