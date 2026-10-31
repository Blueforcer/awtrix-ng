#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace awtrix::tc002::wake {

// A microWakeWord streaming model: an int8 TensorFlow Lite flatbuffer with input [1, N, 40] and one
// output, its streaming state in resource variables that a CALL_ONCE subgraph sets up. load() checks
// the whole file, refuses anything it cannot run exactly with a short reason and plans one arena;
// invoke() then runs the graph with the arithmetic of TensorFlow Lite's reference kernels, bit for
// bit, and never allocates. Not thread-safe; one model per stream.
class WakeModel {
 public:
  // The ops a model may use. Everything else fails to load, naming the op.
  // ADD, ASSIGN_VARIABLE, AVERAGE_POOL_2D, CALL_ONCE, CONCATENATION, CONV_2D, DEPTHWISE_CONV_2D,
  // FULLY_CONNECTED, LOGISTIC, MAX_POOL_2D, MEAN, MUL, QUANTIZE, READ_VARIABLE, RESHAPE, SPLIT_V,
  // STRIDED_SLICE, VAR_HANDLE.
  static constexpr std::size_t kFeatures = 40;

  // Copies the file; nullptr and a reason when it is malformed or not supported.
  static std::unique_ptr<WakeModel> load(const uint8_t* data, std::size_t size, std::string& error);
  ~WakeModel();

  // N: feature frames per invoke, the model's stride.
  std::size_t frames() const { return frames_; }
  float inputScale() const { return inputScale_; }
  int32_t inputZeroPoint() const { return inputZeroPoint_; }
  // N * 40 int8 values in frame order, to be filled before each invoke.
  int8_t* input() { return input_; }

  // Runs the graph once and returns the first output byte: the uint8 probability of a wake model.
  uint8_t invoke();
  const uint8_t* output() const { return output_; }
  std::size_t outputSize() const { return outputSize_; }
  bool outputIsUint8() const { return outputUint8_; }

  // The streaming state back to what the init subgraph sets, as in a freshly loaded model.
  void reset();

  // Bytes of activations and state, planned once at load.
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
