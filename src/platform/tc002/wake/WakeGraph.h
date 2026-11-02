#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/tc002/wake/WakeFlat.h"
#include "platform/tc002/wake/WakeModel.h"

namespace awtrix::tc002::wake {

constexpr std::size_t kMaxRank = 6;
constexpr int64_t kMaxDim = int64_t{1} << 20;
constexpr int kNoProducer = -2, kGraphInputProducer = -1;

enum TensorType : int { kFloat32 = 0, kInt32 = 2, kUInt8 = 3, kInt8 = 9, kResource = 13 };

enum Code : int {
  kAveragePool = 1,
  kConcatenation = 2,
  kConv = 3,
  kDepthwise = 4,
  kFullyConnected = 9,
  kLogistic = 14,
  kReshape = 22,
  kStridedSlice = 45,
  kSplitV = 102,
  kQuantize = 114,
  kCallOnce = 129,
  kVarHandle = 142,
  kReadVariable = 143,
  kAssignVariable = 144,
};

struct Scale {
  int32_t multiplier = 0;
  int shift = 0;
};

struct Tensor {
  int type = -1;
  std::size_t rank = 0;
  std::array<int32_t, kMaxRank> dims{};
  std::size_t elements = 1, bytes = 0;
  std::vector<float> scales;
  std::vector<int64_t> zeroPoints;
  int quantDim = 0;
  const uint8_t* constant = nullptr;
  int producer = kNoProducer;
  int lastUse = -1;
  bool needsMemory = false;
  std::size_t offset = 0;
  uint8_t* data = nullptr;
  int variable = -1;

  float scale() const { return scales.empty() ? 0.0f : scales[0]; }
  int32_t zeroPoint() const { return zeroPoints.empty() ? 0 : static_cast<int32_t>(zeroPoints[0]); }
  int32_t dim(std::size_t i) const { return dims[i]; }
  const int8_t* i8() const { return reinterpret_cast<const int8_t*>(data); }
  int8_t* o8() const { return reinterpret_cast<int8_t*>(data); }
};

struct Shape6 {
  std::array<int32_t, kMaxRank> dims{};
};

struct Node {
  int code = 0;
  std::vector<int> in, out;
  Flat::Table options;
  int optionsType = 0;

  int32_t actMin = -128, actMax = 127;
  int32_t inOffset = 0, outOffset = 0, filterOffset = 0;
  int strideH = 1, strideW = 1, dilationH = 1, dilationW = 1, padH = 0, padW = 0;
  int filterH = 0, filterW = 0, depthMultiplier = 1, groups = 1;
  std::vector<Scale> channelScales;
  std::vector<int32_t> bias;
  int units = 0;
  std::vector<double> realScales;
  Scale scaleOut;
  std::vector<int8_t> lut;
  Shape6 shape1;
  std::array<int32_t, kMaxRank> start{}, step{}, count{};
  std::size_t outer = 1, inner = 1;
  std::vector<std::size_t> parts;
  int subgraph = -1;
};

struct WakeModel::Graph {
  std::vector<Tensor> tensors;
  std::vector<Node> nodes;
  std::vector<int> inputs, outputs;
};

}
