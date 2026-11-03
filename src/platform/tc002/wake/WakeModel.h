#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace awtrix::posix {
class MappedFile;
}

namespace awtrix::tc002::wake {

class WakeModel {
 public:
  static constexpr std::size_t kFeatures = 40;

  // A model in memory, which it copies.
  static std::unique_ptr<WakeModel> load(const uint8_t* data, std::size_t size, std::string& error);
  // A model in a file, which it keeps: the graphs only read their constants, the loader refuses a
  // constant as any operator's output.
  static std::unique_ptr<WakeModel> load(std::shared_ptr<const posix::MappedFile> file, std::string& error);
  ~WakeModel();

  std::size_t frames() const { return frames_; }
  float inputScale() const { return inputScale_; }
  int32_t inputZeroPoint() const { return inputZeroPoint_; }
  int8_t* input() { return input_; }

  uint8_t invoke();
  void reset();

  struct Graph;

 private:
  WakeModel() = default;
  // The model of a file this model already holds.
  static std::unique_ptr<WakeModel> prepare(std::unique_ptr<WakeModel> model, const uint8_t* data, std::size_t size,
                                            std::string& error);
  void run(std::size_t graph);

  std::vector<uint8_t> copy_;
  std::shared_ptr<const posix::MappedFile> mapped_;
  std::vector<uint8_t> arena_;
  std::vector<Graph> graphs_;
  std::vector<uint8_t> called_;
  int8_t* input_ = nullptr;
  const uint8_t* output_ = nullptr;
  std::size_t frames_ = 0;
  float inputScale_ = 0.0f;
  int32_t inputZeroPoint_ = 0;
};

}
