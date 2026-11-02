// The tensor checks and quantised multipliers follow TensorFlow Lite (tensorflow/lite/kernels, Copyright The
// TensorFlow Authors, Apache-2.0).
#include "platform/tc002/wake/WakeOperators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace awtrix::tc002::wake {
namespace {

constexpr int64_t kMaxProductsPerAccumulator = int64_t{1} << 16;

bool quantizeMultiplier(double value, Scale& out) {
  out = {};
  if (value == 0.0) return true;
  if (!std::isfinite(value) || value < 0.0) return false;
  int shift = 0;
  const double fraction = std::frexp(value, &shift);
  int64_t fixed = static_cast<int64_t>(std::round(fraction * static_cast<double>(int64_t{1} << 31)));
  if (fixed == (int64_t{1} << 31)) {
    fixed /= 2;
    ++shift;
  }
  if (shift < -31) return true;
  if (shift > 30) return false;
  out = {static_cast<int32_t>(fixed), shift};
  return true;
}

Shape6 extend(const Tensor& t) {
  Shape6 s;
  s.dims.fill(1);
  for (std::size_t i = 0; i < t.rank; ++i) s.dims[kMaxRank - t.rank + i] = t.dims[i];
  return s;
}

bool count(const Node& node, std::size_t inputs, std::size_t outputs) {
  if (node.in.size() != inputs || node.out.size() != outputs)
    return false;
  for (int index : node.in)
    if (index < 0) return false;
  return true;
}

bool options(Node& node, int type) {
  if (node.optionsType != 0 && node.optionsType != type) return false;
  if (node.optionsType == 0) node.options = {};
  return true;
}

bool perTensor(const Tensor& t) {
  if (t.scales.size() != 1 || t.zeroPoints.size() != 1) return false;
  const int32_t lowest = t.type == kUInt8 ? 0 : -128, highest = t.type == kUInt8 ? 255 : 127;
  return t.zeroPoint() >= lowest && t.zeroPoint() <= highest;
}

bool activation(Node& node, int function, const Tensor& out) {
  constexpr int32_t lowest = -128, highest = 127;
  bool ok = true;
  auto quantized = [&](float f) {
    const float q = std::round(f / out.scale());
    if (!(q >= -2147483648.0f && q <= 2147483520.0f)) ok = false;
    return ok ? out.zeroPoint() + static_cast<int32_t>(q) : 0;
  };
  node.actMin = lowest;
  node.actMax = highest;
  switch (function) {
    case 0: break;
    case 1: node.actMin = std::max(lowest, quantized(0.0f)); break;
    case 2:
      node.actMin = std::max(lowest, quantized(-1.0f));
      node.actMax = std::min(highest, quantized(1.0f));
      break;
    case 3:
      node.actMin = std::max(lowest, quantized(0.0f));
      node.actMax = std::min(highest, quantized(6.0f));
      break;
    default: return false;
  }
  return ok && node.actMin <= node.actMax;
}

bool steps(const Node& node) {
  constexpr int most = 1024;
  return node.strideW >= 1 && node.strideH >= 1 && node.dilationW >= 1 && node.dilationH >= 1 &&
         node.strideW <= most && node.strideH <= most && node.dilationW <= most && node.dilationH <= most;
}

void padding(int same, int stride, int dilation, int in, int filter, int& out, int& pad) {
  const int64_t effective = int64_t{filter - 1} * dilation + 1;
  const int64_t size =
    same == 0 ? (int64_t{in} + stride - 1) / stride : (int64_t{in} + stride - effective) / stride;
  out = static_cast<int>(std::max<int64_t>(size, 0));
  const int64_t total = std::max<int64_t>((int64_t{out} - 1) * stride + effective - in, 0);
  pad = static_cast<int>(total / 2);
}

bool filterScales(const Tensor& filter, int outC, int quantDim) {
  if (filter.scales.size() != 1 && filter.scales.size() != static_cast<std::size_t>(outC))
    return false;
  if (filter.scales.size() > 1 && filter.quantDim != quantDim) return false;
  for (int64_t zero : filter.zeroPoints)
    if (zero != 0) return false;
  return true;
}

class Preparer {
 public:
  Preparer(Flat& flat, WakeModel::Graph& g, std::size_t graphCount, std::vector<Variable>& variables)
      : flat_(flat), g_(g), graphCount_(graphCount), variables_(variables) {}

  bool prepare(Node& node);

 private:
  bool filterInputs(Node& node, int optionsType);
  bool geometry(Node& node, int outC, int functionField, int& function);
  bool readBias(Node& node, int size);
  bool channelScales(Node& node, int outC);
  bool conv(Node& node);
  bool depthwise(Node& node);
  bool fullyConnected(Node& node);
  bool pool(Node& node);
  bool logistic(Node& node);
  bool quantize(Node& node);
  bool concatenation(Node& node);
  bool reshape(Node& node);
  bool stridedSlice(Node& node);
  bool splitV(Node& node);
  bool variableNode(Node& node);
  int variableFor(std::string_view container, std::string_view name);

  Flat& flat_;
  WakeModel::Graph& g_;
  std::size_t graphCount_;
  std::vector<Variable>& variables_;
};

int Preparer::variableFor(std::string_view container, std::string_view name) {
  std::string key(container);
  key += '\n';
  key += name;
  for (std::size_t i = 0; i < variables_.size(); ++i)
    if (variables_[i].key == key) return static_cast<int>(i);
  variables_.push_back({key});
  return static_cast<int>(variables_.size() - 1);
}

bool Preparer::prepare(Node& node) {
  switch (node.code) {
    case kConv: return conv(node);
    case kDepthwise: return depthwise(node);
    case kFullyConnected: return fullyConnected(node);
    case kAveragePool: return pool(node);
    case kLogistic: return logistic(node);
    case kQuantize: return quantize(node);
    case kConcatenation: return concatenation(node);
    case kReshape: return reshape(node);
    case kStridedSlice: return stridedSlice(node);
    case kSplitV: return splitV(node);
    case kVarHandle:
    case kReadVariable:
    case kAssignVariable:
    case kCallOnce: return variableNode(node);
    default: return false;
  }
}

bool Preparer::filterInputs(Node& node, int optionsType) {
  if (node.in.size() != 2 && node.in.size() != 3) return false;
  if (node.out.size() != 1) return false;
  if (!options(node, optionsType)) return false;
  if (node.in[0] < 0 || node.in[1] < 0) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& filter = g_.tensors[node.in[1]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.type != kInt8 || filter.type != kInt8 || out.type != kInt8 || !filter.constant)
    return false;
  return perTensor(in) && perTensor(out);
}

bool Preparer::geometry(Node& node, int outC, int functionField, int& function) {
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& filter = g_.tensors[node.in[1]];
  const Tensor& out = g_.tensors[node.out[0]];
  const int same = flat_.field<int8_t>(node.options, 0, 0);
  node.strideW = flat_.field<int32_t>(node.options, 1, 0);
  node.strideH = flat_.field<int32_t>(node.options, 2, 0);
  function = flat_.field<int8_t>(node.options, functionField, 0);
  node.dilationW = flat_.field<int32_t>(node.options, functionField + 1, 1);
  node.dilationH = flat_.field<int32_t>(node.options, functionField + 2, 1);
  if (same != 0 && same != 1) return false;
  if (!steps(node)) return false;
  int outH = 0, outW = 0;
  padding(same, node.strideH, node.dilationH, in.dim(1), filter.dim(1), outH, node.padH);
  padding(same, node.strideW, node.dilationW, in.dim(2), filter.dim(2), outW, node.padW);
  return out.dim(0) == in.dim(0) && out.dim(1) == outH && out.dim(2) == outW && out.dim(3) == outC;
}

bool Preparer::readBias(Node& node, int size) {
  if (node.in.size() != 3 || node.in[2] < 0) return true;
  const Tensor& bias = g_.tensors[node.in[2]];
  if (bias.type != kInt32 || !bias.constant || bias.elements != static_cast<std::size_t>(size))
    return false;
  node.bias.resize(bias.elements);
  std::memcpy(node.bias.data(), bias.constant, bias.bytes);
  return true;
}

bool Preparer::channelScales(Node& node, int outC) {
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& filter = g_.tensors[node.in[1]];
  const Tensor& out = g_.tensors[node.out[0]];
  node.inOffset = -in.zeroPoint();
  node.outOffset = out.zeroPoint();
  node.channelScales.resize(outC);
  for (int c = 0; c < outC; ++c) {
    const double scale = static_cast<double>(filter.scales.size() > 1 ? filter.scales[c] : filter.scales[0]);
    const double effective = static_cast<double>(in.scale()) * scale / static_cast<double>(out.scale());
    if (!quantizeMultiplier(effective, node.channelScales[c])) return false;
  }
  return true;
}

bool Preparer::conv(Node& node) {
  if (!filterInputs(node, 1)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& filter = g_.tensors[node.in[1]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.rank != 4 || filter.rank != 4 || out.rank != 4) return false;
  const int outC = filter.dim(0), kC = filter.dim(3), inC = in.dim(3);
  int function = 0;
  if (!geometry(node, outC, 3, function)) return false;
  if (inC % kC != 0) return false;
  node.groups = inC / kC;
  if (outC % node.groups != 0) return false;
  if (int64_t{filter.dim(1)} * filter.dim(2) * kC > kMaxProductsPerAccumulator)
    return false;
  if (!filterScales(filter, outC, 0) || !readBias(node, outC) || !channelScales(node, outC)) return false;
  return activation(node, function, out);
}

bool Preparer::depthwise(Node& node) {
  if (!filterInputs(node, 2)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& filter = g_.tensors[node.in[1]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.rank != 4 || filter.rank != 4 || out.rank != 4 || filter.dim(0) != 1)
    return false;
  const int outC = filter.dim(3), inC = in.dim(3);
  int function = 0;
  if (!geometry(node, outC, 4, function)) return false;
  if (outC % inC != 0) return false;
  node.depthMultiplier = outC / inC;
  if (int64_t{filter.dim(1)} * filter.dim(2) > kMaxProductsPerAccumulator) return false;
  if (!filterScales(filter, outC, 3) || !readBias(node, outC) || !channelScales(node, outC)) return false;
  return activation(node, function, out);
}

bool Preparer::fullyConnected(Node& node) {
  if (!filterInputs(node, 8)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& filter = g_.tensors[node.in[1]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (filter.rank != 2) return false;
  const int function = flat_.field<int8_t>(node.options, 0, 0);
  const int format = flat_.field<int8_t>(node.options, 1, 0);
  const bool keepDims = flat_.field<uint8_t>(node.options, 2, 0) != 0;
  if (format != 0) return false;
  node.units = filter.dim(0);
  const std::size_t depth = static_cast<std::size_t>(filter.dim(1));
  if (in.elements % depth != 0) return false;
  node.inner = depth;
  node.outer = in.elements / depth;
  bool shapeOk = false;
  if (keepDims) {
    shapeOk = in.rank >= 1 && out.rank == in.rank && in.dims[in.rank - 1] == filter.dim(1) &&
              out.dims[out.rank - 1] == node.units;
    for (std::size_t i = 0; shapeOk && i + 1 < in.rank; ++i) shapeOk = in.dims[i] == out.dims[i];
  } else {
    shapeOk = out.rank == 2 && static_cast<std::size_t>(out.dim(0)) == node.outer && out.dim(1) == node.units;
  }
  if (!shapeOk) return false;
  if (static_cast<int64_t>(depth) > kMaxProductsPerAccumulator) return false;
  const bool perChannel = filter.scales.size() > 1;
  if (filter.scales.empty()) return false;
  if (perChannel) {
    if (!filterScales(filter, node.units, 0)) return false;
  } else {
    if (filter.zeroPoints.size() != 1 || filter.zeroPoint() < -128 || filter.zeroPoint() > 127)
      return false;
    node.filterOffset = -filter.zeroPoint();
  }
  if (!readBias(node, node.units)) return false;
  if (!perChannel && !node.bias.empty()) {
    const double product = static_cast<double>(in.scale()) * static_cast<double>(filter.scale());
    const double bias = static_cast<double>(g_.tensors[node.in[2]].scale());
    if (std::abs(product - bias) / static_cast<double>(out.scale()) > 0.02)
      return false;
  }
  node.inOffset = -in.zeroPoint();
  node.outOffset = out.zeroPoint();
  for (float scale : filter.scales) {
    const double effective =
      static_cast<double>(in.scale()) * static_cast<double>(scale) / static_cast<double>(out.scale());
    if (!std::isfinite(effective)) return false;
    node.realScales.push_back(effective);
  }
  return activation(node, function, out);
}

bool Preparer::pool(Node& node) {
  if (!count(node, 1, 1) || !options(node, 5)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.type != kInt8 || out.type != kInt8 || in.rank != 4 || out.rank != 4)
    return false;
  if (!perTensor(in) || !perTensor(out)) return false;
  if (std::abs(in.scale() - out.scale()) > 1.0e-6f || in.zeroPoint() != out.zeroPoint())
    return false;
  const int same = flat_.field<int8_t>(node.options, 0, 0);
  node.strideW = flat_.field<int32_t>(node.options, 1, 0);
  node.strideH = flat_.field<int32_t>(node.options, 2, 0);
  node.filterW = flat_.field<int32_t>(node.options, 3, 0);
  node.filterH = flat_.field<int32_t>(node.options, 4, 0);
  const int function = flat_.field<int8_t>(node.options, 5, 0);
  if (same != 0 && same != 1) return false;
  if (!steps(node) || node.filterW < 1 || node.filterH < 1 ||
      int64_t{node.filterW} * node.filterH > kMaxProductsPerAccumulator)
    return false;
  int outH = 0, outW = 0;
  padding(same, node.strideH, 1, in.dim(1), node.filterH, outH, node.padH);
  padding(same, node.strideW, 1, in.dim(2), node.filterW, outW, node.padW);
  if (out.dim(0) != in.dim(0) || out.dim(1) != outH || out.dim(2) != outW || out.dim(3) != in.dim(3))
    return false;
  for (int y = 0; y < outH; ++y) {
    const int origin = y * node.strideH - node.padH;
    if (std::min(node.filterH, in.dim(1) - origin) <= std::max(0, -origin))
      return false;
  }
  for (int x = 0; x < outW; ++x) {
    const int origin = x * node.strideW - node.padW;
    if (std::min(node.filterW, in.dim(2) - origin) <= std::max(0, -origin))
      return false;
  }
  return activation(node, function, out);
}

bool Preparer::logistic(Node& node) {
  if (!count(node, 1, 1)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.type != kInt8 || out.type != kInt8 || in.elements != out.elements)
    return false;
  if (!perTensor(in) || !perTensor(out)) return false;
  if (static_cast<double>(out.scale()) != 1.0 / 256) return false;
  const float inverse = 1 / out.scale();
  node.lut.resize(256);
  for (int32_t value = -128; value <= 127; ++value) {
    const float real = in.scale() * static_cast<float>(value - in.zeroPoint());
    const float transformed = 1.0f / (1.0f + std::exp(-real));
    const float rescaled = std::round(transformed * inverse);
    const int32_t quantized = static_cast<int32_t>(rescaled + static_cast<float>(out.zeroPoint()));
    node.lut[static_cast<uint8_t>(value)] = static_cast<int8_t>(std::max(-128, std::min(127, quantized)));
  }
  return true;
}

bool Preparer::quantize(Node& node) {
  if (!count(node, 1, 1)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& out = g_.tensors[node.out[0]];
  if ((in.type != kInt8 && in.type != kUInt8) || (out.type != kInt8 && out.type != kUInt8) ||
      in.elements != out.elements)
    return false;
  if (!perTensor(in) || !perTensor(out)) return false;
  node.inOffset = in.zeroPoint();
  node.outOffset = out.zeroPoint();
  node.actMin = out.type == kUInt8 ? 0 : -128;
  node.actMax = out.type == kUInt8 ? 255 : 127;
  const double real = static_cast<double>(in.scale()) / static_cast<double>(out.scale());
  if (!quantizeMultiplier(real, node.scaleOut)) return false;
  return true;
}

bool Preparer::concatenation(Node& node) {
  if (node.in.empty() || node.out.size() != 1 || !options(node, 10))
    return false;
  const Tensor& out = g_.tensors[node.out[0]];
  int axis = flat_.field<int32_t>(node.options, 0, 0);
  if (flat_.field<int8_t>(node.options, 1, 0) != 0) return false;
  if (axis < 0) axis += static_cast<int>(out.rank);
  if (axis < 0 || axis >= static_cast<int>(out.rank)) return false;
  int64_t total = 0;
  for (int index : node.in) {
    if (index < 0) return false;
    const Tensor& t = g_.tensors[index];
    if (t.type != out.type || t.rank != out.rank || t.type < 0 || t.type == kResource)
      return false;
    if (out.type == kInt8 && (!perTensor(t) || !perTensor(out) ||
                              t.scale() != out.scale() || t.zeroPoint() != out.zeroPoint()))
      return false;
    for (std::size_t d = 0; d < out.rank; ++d)
      if (static_cast<int>(d) != axis && t.dims[d] != out.dims[d])
        return false;
    total += t.dims[axis];
  }
  if (total != out.dims[axis]) return false;
  const std::size_t width = out.bytes / out.elements;
  node.outer = 1;
  for (int d = 0; d < axis; ++d) node.outer *= static_cast<std::size_t>(out.dims[d]);
  node.inner = width;
  for (std::size_t d = axis + 1; d < out.rank; ++d) node.inner *= static_cast<std::size_t>(out.dims[d]);
  for (int index : node.in)
    node.parts.push_back(static_cast<std::size_t>(g_.tensors[index].dims[axis]) * node.inner);
  return true;
}

bool Preparer::reshape(Node& node) {
  if (node.in.empty() || node.in.size() > 2 || node.out.size() != 1 || node.in[0] < 0)
    return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.type != out.type || in.elements != out.elements || in.type == kResource)
    return false;
  return true;
}

bool Preparer::stridedSlice(Node& node) {
  if (!count(node, 4, 1) || !options(node, 32)) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& out = g_.tensors[node.out[0]];
  if (in.type != out.type || in.type < 0 || in.type == kResource)
    return false;
  const int begins = flat_.field<int32_t>(node.options, 0, 0);
  const int ends = flat_.field<int32_t>(node.options, 1, 0);
  const int shrinks = flat_.field<int32_t>(node.options, 4, 0);
  if (flat_.field<int32_t>(node.options, 2, 0) != 0 || flat_.field<int32_t>(node.options, 3, 0) != 0 ||
      flat_.field<uint8_t>(node.options, 5, 0) != 0)
    return false;
  for (std::size_t i = 1; i < 4; ++i) {
    const Tensor& t = g_.tensors[node.in[i]];
    if (t.type != kInt32 || !t.constant || t.rank != 1 || t.elements != in.rank)
      return false;
  }
  const Tensor& begin = g_.tensors[node.in[1]];
  const Tensor& end = g_.tensors[node.in[2]];
  const Tensor& strides = g_.tensors[node.in[3]];
  std::array<int32_t, kMaxRank> expected{};
  std::size_t kept = 0;
  const std::size_t pad = kMaxRank - in.rank;
  for (std::size_t i = 0; i < kMaxRank; ++i) {
    node.start[i] = 0;
    node.step[i] = 1;
    node.count[i] = 1;
  }
  for (std::size_t axis = 0; axis < in.rank; ++axis) {
    const int64_t size = in.dims[axis];
    int32_t raw = 0;
    std::memcpy(&raw, strides.constant + 4 * axis, 4);
    const int64_t stride = raw;
    if (stride == 0 || stride < -kMaxDim || stride > kMaxDim) return false;
    auto clamp = [&](int64_t v) {
      return stride > 0 ? std::clamp<int64_t>(v, 0, size) : std::clamp<int64_t>(v, -1, size - 1);
    };
    int32_t b = 0, e = 0;
    std::memcpy(&b, begin.constant + 4 * axis, 4);
    std::memcpy(&e, end.constant + 4 * axis, 4);
    int64_t first = b;
    if (begins & (1 << axis)) {
      first = stride > 0 ? 0 : size - 1;
    } else {
      if (first < 0) first += size;
      first = clamp(first);
    }
    int64_t elements = 0;
    if (shrinks & (1 << axis)) {
      if (first < 0 || first >= size) return false;
      elements = 1;
    } else {
      int64_t last = e;
      if (ends & (1 << axis)) {
        last = stride > 0 ? size : -1;
      } else {
        if (last < 0) last += size;
        last = clamp(last);
      }
      elements = stride > 0 ? (last - first + stride - 1) / stride : (first - last - stride - 1) / -stride;
      if (elements < 1) return false;
      expected[kept++] = static_cast<int32_t>(elements);
    }
    node.start[pad + axis] = static_cast<int32_t>(first);
    node.step[pad + axis] = static_cast<int32_t>(stride);
    node.count[pad + axis] = static_cast<int32_t>(elements);
  }
  if (out.rank != kept) return false;
  for (std::size_t i = 0; i < kept; ++i)
    if (out.dims[i] != expected[i]) return false;
  node.shape1 = extend(in);
  return true;
}

bool Preparer::splitV(Node& node) {
  if (node.in.size() != 3 || node.out.empty() || !options(node, 79))
    return false;
  for (int index : node.in)
    if (index < 0) return false;
  const Tensor& in = g_.tensors[node.in[0]];
  const Tensor& sizes = g_.tensors[node.in[1]];
  const Tensor& axisTensor = g_.tensors[node.in[2]];
  if (in.type < 0 || in.type == kResource) return false;
  if (flat_.field<int32_t>(node.options, 0, 0) != static_cast<int32_t>(node.out.size()))
    return false;
  if (sizes.type != kInt32 || !sizes.constant || sizes.rank != 1 || sizes.elements != node.out.size())
    return false;
  if (axisTensor.type != kInt32 || !axisTensor.constant || axisTensor.elements != 1)
    return false;
  int32_t axis = 0;
  std::memcpy(&axis, axisTensor.constant, 4);
  if (axis < 0) axis += static_cast<int32_t>(in.rank);
  if (axis < 0 || axis >= static_cast<int32_t>(in.rank)) return false;
  std::vector<int64_t> parts(node.out.size());
  int64_t known = 0;
  int unknown = -1;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    int32_t size = 0;
    std::memcpy(&size, sizes.constant + 4 * i, 4);
    if (size == -1 && unknown < 0) {
      unknown = static_cast<int>(i);
    } else if (size < 0) {
      return false;
    } else {
      known += size;
    }
    parts[i] = size;
  }
  if (unknown >= 0) parts[unknown] = in.dims[axis] - known;
  int64_t total = 0;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const Tensor& out = g_.tensors[node.out[i]];
    if (parts[i] < 1 || out.type != in.type || out.rank != in.rank)
      return false;
    for (std::size_t d = 0; d < in.rank; ++d) {
      const int64_t want = static_cast<int32_t>(d) == axis ? parts[i] : in.dims[d];
      if (out.dims[d] != want) return false;
    }
    total += parts[i];
  }
  if (total != in.dims[axis]) return false;
  const std::size_t width = in.bytes / in.elements;
  node.outer = 1;
  for (int32_t d = 0; d < axis; ++d) node.outer *= static_cast<std::size_t>(in.dims[d]);
  node.inner = width;
  for (std::size_t d = axis + 1; d < in.rank; ++d) node.inner *= static_cast<std::size_t>(in.dims[d]);
  for (int64_t part : parts) node.parts.push_back(static_cast<std::size_t>(part) * node.inner);
  return true;
}

bool Preparer::variableNode(Node& node) {
  switch (node.code) {
    case kVarHandle: {
      if (!count(node, 0, 1) || !options(node, 111)) return false;
      Tensor& handle = g_.tensors[node.out[0]];
      if (handle.type != kResource) return false;
      handle.variable = variableFor(flat_.string(node.options, 0), flat_.string(node.options, 1));
      return flat_.ok();
    }
    case kReadVariable:
    case kAssignVariable: {
      const bool read = node.code == kReadVariable;
      if (!count(node, read ? 1 : 2, read ? 1 : 0)) return false;
      const Tensor& handle = g_.tensors[node.in[0]];
      const Tensor& value = g_.tensors[read ? node.out[0] : node.in[1]];
      if (handle.type != kResource || handle.variable < 0) return false;
      if (value.type < 0 || value.type == kResource) return false;
      Variable& v = variables_[handle.variable];
      if (v.type >= 0 && (v.type != value.type || v.bytes != value.bytes))
        return false;
      v.type = value.type;
      v.bytes = value.bytes;
      return true;
    }
    default: {
      if (!count(node, 0, 0) || !options(node, 103)) return false;
      node.subgraph = flat_.field<int32_t>(node.options, 0, 0);
      if (node.subgraph < 1 || static_cast<std::size_t>(node.subgraph) >= graphCount_)
        return false;
      return true;
    }
  }
}

}

bool prepareOperator(Flat& flat, WakeModel::Graph& g, Node& node, std::size_t graphCount,
                     std::vector<Variable>& variables) {
  return Preparer(flat, g, graphCount, variables).prepare(node);
}

}
