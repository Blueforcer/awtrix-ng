// The int8 kernels follow TensorFlow Lite's reference kernels (tensorflow/lite/kernels, Copyright The
// TensorFlow Authors, Apache-2.0) as LiteRT 2.3 runs them with OpResolverType.BUILTIN_REF, including
// gemmlowp's double-rounding fixed-point multiply and the float requantisation of FULLY_CONNECTED.
#include "platform/tc002/wake/WakeModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace awtrix::tc002::wake {
namespace {

constexpr std::size_t kMaxRank = 6;
constexpr std::size_t kMaxGraphs = 8;
constexpr std::size_t kMaxTensors = 4096;
constexpr std::size_t kMaxNodes = 1024;
constexpr int64_t kMaxDim = int64_t{1} << 20;
constexpr std::size_t kMaxElements = std::size_t{1} << 22;
constexpr std::size_t kMaxArena = std::size_t{64} << 20;
// Products summed into one int32 accumulator; 2^16 of them cannot overflow it.
constexpr int64_t kMaxAccumulation = int64_t{1} << 16;
constexpr std::size_t kAlign = 16;

enum TensorType : int { kFloat32 = 0, kInt32 = 2, kUInt8 = 3, kInt8 = 9, kResource = 13 };

enum Code : int {
  kAdd = 0,
  kAveragePool = 1,
  kConcatenation = 2,
  kConv = 3,
  kDepthwise = 4,
  kFullyConnected = 9,
  kLogistic = 14,
  kMaxPool = 17,
  kMul = 18,
  kReshape = 22,
  kCustom = 32,
  kMean = 40,
  kStridedSlice = 45,
  kSplitV = 102,
  kQuantize = 114,
  kCallOnce = 129,
  kVarHandle = 142,
  kReadVariable = 143,
  kAssignVariable = 144,
};

// Builtin operator names 0 to 161 in schema order, "-" for unused codes.
constexpr std::string_view kOpNames =
  "ADD AVERAGE_POOL_2D CONCATENATION CONV_2D DEPTHWISE_CONV_2D DEPTH_TO_SPACE DEQUANTIZE EMBEDDING_LOOKUP "
  "FLOOR FULLY_CONNECTED HASHTABLE_LOOKUP L2_NORMALIZATION L2_POOL_2D LOCAL_RESPONSE_NORMALIZATION LOGISTIC "
  "LSH_PROJECTION LSTM MAX_POOL_2D MUL RELU RELU_N1_TO_1 RELU6 RESHAPE RESIZE_BILINEAR RNN SOFTMAX "
  "SPACE_TO_DEPTH SVDF TANH CONCAT_EMBEDDINGS SKIP_GRAM CALL CUSTOM EMBEDDING_LOOKUP_SPARSE PAD "
  "UNIDIRECTIONAL_SEQUENCE_RNN GATHER BATCH_TO_SPACE_ND SPACE_TO_BATCH_ND TRANSPOSE MEAN SUB DIV SQUEEZE "
  "UNIDIRECTIONAL_SEQUENCE_LSTM STRIDED_SLICE BIDIRECTIONAL_SEQUENCE_RNN EXP TOPK_V2 SPLIT LOG_SOFTMAX "
  "DELEGATE BIDIRECTIONAL_SEQUENCE_LSTM CAST PRELU MAXIMUM ARG_MAX MINIMUM LESS NEG PADV2 GREATER "
  "GREATER_EQUAL LESS_EQUAL SELECT SLICE SIN TRANSPOSE_CONV SPARSE_TO_DENSE TILE EXPAND_DIMS EQUAL NOT_EQUAL "
  "LOG SUM SQRT RSQRT SHAPE POW ARG_MIN FAKE_QUANT REDUCE_PROD REDUCE_MAX PACK LOGICAL_OR ONE_HOT "
  "LOGICAL_AND LOGICAL_NOT UNPACK REDUCE_MIN FLOOR_DIV REDUCE_ANY SQUARE ZEROS_LIKE FILL FLOOR_MOD RANGE "
  "RESIZE_NEAREST_NEIGHBOR LEAKY_RELU SQUARED_DIFFERENCE MIRROR_PAD ABS SPLIT_V UNIQUE CEIL REVERSE_V2 ADD_N "
  "GATHER_ND COS WHERE RANK ELU REVERSE_SEQUENCE MATRIX_DIAG QUANTIZE MATRIX_SET_DIAG ROUND HARD_SWISH IF "
  "WHILE NON_MAX_SUPPRESSION_V4 NON_MAX_SUPPRESSION_V5 SCATTER_ND SELECT_V2 DENSIFY SEGMENT_SUM BATCH_MATMUL "
  "PLACEHOLDER_FOR_GREATER_OP_CODES CUMSUM CALL_ONCE BROADCAST_TO RFFT2D CONV_3D - - - HASHTABLE "
  "HASHTABLE_FIND HASHTABLE_IMPORT HASHTABLE_SIZE REDUCE_ALL CONV_3D_TRANSPOSE VAR_HANDLE READ_VARIABLE "
  "ASSIGN_VARIABLE BROADCAST_ARGS RANDOM_STANDARD_NORMAL BUCKETIZE RANDOM_UNIFORM MULTINOMIAL GELU "
  "DYNAMIC_UPDATE_SLICE RELU_0_TO_1 UNSORTED_SEGMENT_PROD UNSORTED_SEGMENT_MAX UNSORTED_SEGMENT_SUM ATAN2 "
  "UNSORTED_SEGMENT_MIN SIGN BITCAST BITWISE_XOR RIGHT_SHIFT";

std::string opName(int code) {
  std::size_t at = 0;
  for (int i = 0; code >= 0 && at < kOpNames.size(); ++i) {
    const std::size_t end = std::min(kOpNames.find(' ', at), kOpNames.size());
    if (i == code && kOpNames.substr(at, end - at) != "-") return std::string(kOpNames.substr(at, end - at));
    at = end + 1;
  }
  return "builtin operator " + std::to_string(code);
}

// A bounds-checked reader of the flatbuffer: every failed check clears ok() and later reads return
// defaults, so the parser checks ok() once per structure instead of after every field.
class Flat {
 public:
  struct Table {
    std::size_t pos = 0, vtable = 0, vsize = 0, tsize = 0;
    bool present = false;
  };
  struct Vector {
    std::size_t pos = 0, count = 0;
  };

  Flat(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  bool ok() const { return ok_; }

  Table root() {
    uint32_t offset = 0;
    if (!read(0, offset)) return {};
    return tableAt(offset);
  }
  template <typename T>
  T field(const Table& t, int id, T fallback) {
    T value = fallback;
    if (const std::size_t at = slot(t, id, sizeof(T))) read(at, value);
    return ok_ ? value : fallback;
  }
  Table table(const Table& t, int id) {
    const std::size_t at = slot(t, id, 4);
    return at ? tableAt(target(at)) : Table{};
  }
  Vector vector(const Table& t, int id, std::size_t width) {
    const std::size_t at = slot(t, id, 4);
    return at ? vectorAt(target(at), width) : Vector{};
  }
  std::string_view string(const Table& t, int id) {
    const Vector v = vector(t, id, 1);
    return ok_ ? std::string_view(reinterpret_cast<const char*>(data_ + v.pos), v.count) : std::string_view();
  }
  Table tableIn(const Vector& v, std::size_t i) {
    return i < v.count ? tableAt(target(v.pos + 4 * i)) : Table{};
  }
  template <typename T>
  T element(const Vector& v, std::size_t i) {
    T value{};
    if (i < v.count) read(v.pos + i * sizeof(T), value);
    return value;
  }
  const uint8_t* at(std::size_t pos) const { return data_ + pos; }
  bool contains(uint64_t pos, uint64_t length) const { return pos <= size_ && length <= size_ - pos; }

 private:
  template <typename T>
  bool read(std::size_t pos, T& out) {
    if (!ok_ || !contains(pos, sizeof(T))) return ok_ = false;
    std::memcpy(&out, data_ + pos, sizeof(T));
    return true;
  }
  std::size_t target(std::size_t at) {
    uint32_t offset = 0;
    if (!read(at, offset)) return 0;
    if (offset == 0 || !contains(at, offset)) {
      ok_ = false;
      return 0;
    }
    return at + offset;
  }
  Table tableAt(std::size_t pos) {
    int32_t back = 0;
    if (!ok_ || !read(pos, back)) return {};
    const int64_t vtable = static_cast<int64_t>(pos) - back;
    uint16_t vsize = 0, tsize = 0;
    if (vtable < 0 || !read(static_cast<std::size_t>(vtable), vsize) ||
        !read(static_cast<std::size_t>(vtable) + 2, tsize))
      return {};
    if (vsize < 4 || (vsize & 1) || tsize < 4 || !contains(static_cast<std::size_t>(vtable), vsize) ||
        !contains(pos, tsize)) {
      ok_ = false;
      return {};
    }
    return {pos, static_cast<std::size_t>(vtable), vsize, tsize, true};
  }
  std::size_t slot(const Table& t, int id, std::size_t width) {
    if (!ok_ || !t.present) return 0;
    const std::size_t entry = 4 + 2 * static_cast<std::size_t>(id);
    if (entry + 2 > t.vsize) return 0;
    uint16_t offset = 0;
    if (!read(t.vtable + entry, offset) || offset == 0) return 0;
    if (offset + width > t.tsize) {
      ok_ = false;
      return 0;
    }
    return t.pos + offset;
  }
  Vector vectorAt(std::size_t pos, std::size_t width) {
    uint32_t count = 0;
    if (!ok_ || !read(pos, count)) return {};
    if (count > (size_ - pos - 4) / width) {
      ok_ = false;
      return {};
    }
    return {pos + 4, count};
  }

  const uint8_t* data_;
  std::size_t size_;
  bool ok_ = true;
};

// gemmlowp's fixed-point helpers and TensorFlow Lite's double-rounding MultiplyByQuantizedMultiplier.
int32_t doublingHighMul(int32_t a, int32_t b) {
  const bool overflow = a == b && a == std::numeric_limits<int32_t>::min();
  const int64_t product = int64_t{a} * b;
  const int32_t nudge = product >= 0 ? (1 << 30) : (1 - (1 << 30));
  const int32_t high = static_cast<int32_t>((product + nudge) / (int64_t{1} << 31));
  return overflow ? std::numeric_limits<int32_t>::max() : high;
}

int32_t roundingDivideByPot(int32_t x, int exponent) {
  const int32_t mask = static_cast<int32_t>((int64_t{1} << exponent) - 1);
  const int32_t remainder = x & mask;
  const int32_t threshold = (mask >> 1) + (x < 0 ? 1 : 0);
  return (x >> exponent) + (remainder > threshold ? 1 : 0);
}

struct Scale {
  int32_t multiplier = 0;
  int shift = 0;
};

int32_t multiply(int32_t x, const Scale& s) {
  const int left = s.shift > 0 ? s.shift : 0;
  const int right = s.shift > 0 ? 0 : -s.shift;
  return roundingDivideByPot(
    doublingHighMul(static_cast<int32_t>(static_cast<uint32_t>(x) << left), s.multiplier), right);
}

int32_t wrapAdd(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// QuantizeMultiplier, refusing what MultiplyByQuantizedMultiplier cannot take.
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

struct Tensor {
  int type = -1;
  std::size_t rank = 0;
  std::array<int32_t, kMaxRank> dims{};
  std::size_t elements = 1, bytes = 0;
  std::vector<float> scales;
  std::vector<int64_t> zeroPoints;
  int quantDim = 0;
  const uint8_t* constant = nullptr;
  int producer = -2;  // -2 nobody, -1 a graph input, else the node
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

Shape6 extend(const Tensor& t) {
  Shape6 s;
  s.dims.fill(1);
  for (std::size_t i = 0; i < t.rank; ++i) s.dims[kMaxRank - t.rank + i] = t.dims[i];
  return s;
}

std::array<std::size_t, kMaxRank> stridesOf(const Shape6& s) {
  std::array<std::size_t, kMaxRank> strides{};
  std::size_t step = 1;
  for (std::size_t i = kMaxRank; i-- > 0;) {
    strides[i] = step;
    step *= static_cast<std::size_t>(s.dims[i]);
  }
  return strides;
}

}

struct Node {
  int code = 0;
  std::vector<int> in, out;
  Flat::Table options;
  int optionsType = 0;

  int32_t actMin = -128, actMax = 127;
  int32_t inOffset = 0, in2Offset = 0, outOffset = 0, filterOffset = 0;
  int strideH = 1, strideW = 1, dilationH = 1, dilationW = 1, padH = 0, padW = 0;
  int filterH = 0, filterW = 0, depthMultiplier = 1, groups = 1;
  std::vector<Scale> channelScales;
  std::vector<int32_t> bias;
  int units = 0;
  std::vector<double> realScales;
  Scale scale1, scale2, scaleOut;
  int leftShift = 0;
  std::array<int8_t, 256> lut{};
  Shape6 shape1, shape2, shapeOut;
  std::array<int32_t, kMaxRank> start{}, step{}, count{};
  std::size_t outer = 1, inner = 1;
  std::vector<std::size_t> parts;
  std::array<bool, kMaxRank> reduced{};
  int64_t reducedCount = 1;
  std::vector<int32_t> sums;
  int subgraph = -1;
};

struct WakeModel::Graph {
  std::vector<Tensor> tensors;
  std::vector<Node> nodes;
  std::vector<int> inputs, outputs;
};

namespace {

struct Variable {
  std::string key;
  int type = -1;
  std::size_t bytes = 0;
  bool assigned = false;
  std::size_t offset = 0;
};

// One load: parses the file into graphs, checks and prepares every node, plans the arena.
class Loader {
 public:
  Loader(const std::vector<uint8_t>& file, std::vector<WakeModel::Graph>& graphs, std::string& error)
      : flat_(file.data(), file.size()), graphs_(graphs), error_(error) {}

  bool parse();
  bool prepare();
  bool plan(std::vector<uint8_t>& arena);

 private:
  bool fail(std::string message) {
    if (error_.empty()) error_ = std::move(message);
    return false;
  }
  bool failNode(const Node& node, const std::string& message) {
    return fail(opName(node.code) + ": " + message);
  }
  bool parseTensor(const Flat::Table& t, Tensor& out, std::size_t index);
  bool prepareNode(WakeModel::Graph& g, Node& node);
  bool conv(WakeModel::Graph& g, Node& node);
  bool depthwise(WakeModel::Graph& g, Node& node);
  bool fullyConnected(WakeModel::Graph& g, Node& node);
  bool pool(WakeModel::Graph& g, Node& node);
  bool elementwise(WakeModel::Graph& g, Node& node);
  bool logistic(WakeModel::Graph& g, Node& node);
  bool quantize(WakeModel::Graph& g, Node& node);
  bool concatenation(WakeModel::Graph& g, Node& node);
  bool reshape(WakeModel::Graph& g, Node& node);
  bool stridedSlice(WakeModel::Graph& g, Node& node);
  bool splitV(WakeModel::Graph& g, Node& node);
  bool mean(WakeModel::Graph& g, Node& node);
  bool variableNode(WakeModel::Graph& g, Node& node);
  bool checkVariables();
  bool simulate(std::size_t graph, bool nested);
  bool activation(Node& node, int function, const Tensor& out);
  bool count(const Node& node, std::size_t inputs, std::size_t outputs);
  bool options(Node& node, int type);
  bool perTensor(const Node& node, const Tensor& t, const char* what);
  int variableFor(std::string_view container, std::string_view name);
  bool steps(const Node& node);
  void padding(int padding, int stride, int dilation, int in, int filter, int& out, int& pad);

  Flat flat_;
  std::vector<WakeModel::Graph>& graphs_;
  std::string& error_;
  std::vector<int> codes_;
  std::vector<std::string> customNames_;
  Flat::Vector buffers_;

 public:
  std::vector<Variable> variables;
};

bool Loader::parse() {
  const Flat::Table model = flat_.root();
  const uint32_t version = flat_.field<uint32_t>(model, 0, 0);
  const Flat::Vector codes = flat_.vector(model, 1, 4);
  const Flat::Vector subgraphs = flat_.vector(model, 2, 4);
  buffers_ = flat_.vector(model, 4, 4);
  if (!flat_.ok() || !model.present) return fail("not a TensorFlow Lite model");
  if (version != 3) return fail("unsupported schema version " + std::to_string(version));
  if (subgraphs.count == 0 || subgraphs.count > kMaxGraphs) return fail("bad subgraph count");
  if (codes.count > 256) return fail("too many operator codes");

  for (std::size_t i = 0; i < codes.count; ++i) {
    const Flat::Table code = flat_.tableIn(codes, i);
    const int deprecated = flat_.field<int8_t>(code, 0, 0);
    const int builtin = flat_.field<int32_t>(code, 3, 0);
    codes_.push_back(std::max(deprecated, builtin));
    customNames_.emplace_back(flat_.string(code, 1));
  }

  graphs_.resize(subgraphs.count);
  for (std::size_t s = 0; s < subgraphs.count; ++s) {
    WakeModel::Graph& g = graphs_[s];
    const Flat::Table sub = flat_.tableIn(subgraphs, s);
    const Flat::Vector tensors = flat_.vector(sub, 0, 4);
    const Flat::Vector inputs = flat_.vector(sub, 1, 4);
    const Flat::Vector outputs = flat_.vector(sub, 2, 4);
    const Flat::Vector nodes = flat_.vector(sub, 3, 4);
    if (!flat_.ok()) return fail("malformed subgraph");
    if (tensors.count > kMaxTensors || nodes.count > kMaxNodes) return fail("subgraph too large");
    g.tensors.resize(tensors.count);
    for (std::size_t t = 0; t < tensors.count; ++t) {
      if (!parseTensor(flat_.tableIn(tensors, t), g.tensors[t], t)) return false;
    }
    auto indices = [&](const Flat::Vector& v, std::vector<int>& out, bool optional) {
      if (v.count > 64) return false;
      for (std::size_t i = 0; i < v.count; ++i) {
        const int32_t index = flat_.element<int32_t>(v, i);
        if (!(optional && index == -1) && (index < 0 || static_cast<std::size_t>(index) >= g.tensors.size()))
          return false;
        out.push_back(index);
      }
      return flat_.ok();
    };
    if (!indices(inputs, g.inputs, false) || !indices(outputs, g.outputs, false))
      return fail("bad subgraph input or output");
    g.nodes.resize(nodes.count);
    for (std::size_t n = 0; n < nodes.count; ++n) {
      Node& node = g.nodes[n];
      const Flat::Table op = flat_.tableIn(nodes, n);
      const uint32_t opcode = flat_.field<uint32_t>(op, 0, 0);
      if (!flat_.ok() || opcode >= codes_.size()) return fail("bad operator code index");
      node.code = codes_[opcode];
      if (node.code == kCustom) return fail("unsupported custom operator " + customNames_[opcode]);
      if (!indices(flat_.vector(op, 1, 4), node.in, true) ||
          !indices(flat_.vector(op, 2, 4), node.out, false))
        return fail("bad operator tensors");
      node.optionsType = flat_.field<uint8_t>(op, 3, 0);
      node.options = flat_.table(op, 4);
      if (flat_.field<uint8_t>(op, 11, 0) != 0) return fail(opName(node.code) + ": unsupported options");
      if (!flat_.ok()) return fail("malformed operator");
    }
  }
  return true;
}

bool Loader::parseTensor(const Flat::Table& t, Tensor& out, std::size_t index) {
  const std::string name = "tensor " + std::to_string(index) + ": ";
  const Flat::Vector shape = flat_.vector(t, 0, 4);
  out.type = flat_.field<int8_t>(t, 1, 0);
  const uint32_t buffer = flat_.field<uint32_t>(t, 2, 0);
  const Flat::Table quant = flat_.table(t, 4);
  if (flat_.table(t, 6).present) return fail(name + "sparse tensors are not supported");
  if (flat_.field<uint32_t>(t, 10, 0) != 0) return fail(name + "external buffers are not supported");
  if (!flat_.ok()) return fail(name + "malformed");
  if (shape.count > kMaxRank) return fail(name + "rank above 6");
  out.rank = shape.count;
  for (std::size_t i = 0; i < shape.count; ++i) {
    const int32_t dim = flat_.element<int32_t>(shape, i);
    if (dim < 1 || dim > kMaxDim) return fail(name + "bad dimension");
    out.dims[i] = dim;
    out.elements *= static_cast<std::size_t>(dim);
    if (out.elements > kMaxElements) return fail(name + "too large");
  }
  std::size_t width = 0;
  switch (out.type) {
    case kInt8:
    case kUInt8: width = 1; break;
    case kInt32:
    case kFloat32: width = 4; break;
    case kResource: width = 0; break;
    default: width = 0; out.type = -1; break;
  }
  out.bytes = out.elements * width;

  if (quant.present) {
    const Flat::Vector scales = flat_.vector(quant, 2, 4);
    const Flat::Vector zeros = flat_.vector(quant, 3, 8);
    out.quantDim = flat_.field<int32_t>(quant, 6, 0);
    if (flat_.field<uint8_t>(quant, 4, 0) != 0) return fail(name + "custom quantization is not supported");
    if (!flat_.ok() || scales.count > 4096 || zeros.count > 4096)
      return fail(name + "malformed quantization");
    for (std::size_t i = 0; i < scales.count; ++i) {
      const float s = flat_.element<float>(scales, i);
      if (!std::isfinite(s) || s <= 0.0f) return fail(name + "bad scale");
      out.scales.push_back(s);
    }
    for (std::size_t i = 0; i < zeros.count; ++i) out.zeroPoints.push_back(flat_.element<int64_t>(zeros, i));
    if (!out.scales.empty() && out.zeroPoints.size() != out.scales.size() && out.zeroPoints.size() > 1)
      return fail(name + "malformed quantization");
    for (int64_t zero : out.zeroPoints)
      if (zero < -32768 || zero > 65535) return fail(name + "bad zero point");
  }

  if (buffer != 0) {
    if (buffer >= buffers_.count) return fail(name + "bad buffer");
    const Flat::Table b = flat_.tableIn(buffers_, buffer);
    const Flat::Vector data = flat_.vector(b, 0, 1);
    const uint64_t offset = flat_.field<uint64_t>(b, 1, 0);
    const uint64_t size = flat_.field<uint64_t>(b, 2, 0);
    if (!flat_.ok()) return fail(name + "malformed buffer");
    if (data.count > 0) {
      if (data.count != out.bytes) return fail(name + "constant size does not match its shape");
      out.constant = flat_.at(data.pos);
    } else if (offset > 1) {
      if (size != out.bytes || !flat_.contains(offset, size)) return fail(name + "bad buffer offset");
      out.constant = flat_.at(static_cast<std::size_t>(offset));
    }
    if (out.constant && (out.type < 0 || out.type == kResource || out.bytes == 0))
      return fail(name + "constant of an unsupported type");
  }
  return true;
}

bool Loader::count(const Node& node, std::size_t inputs, std::size_t outputs) {
  if (node.in.size() != inputs || node.out.size() != outputs)
    return failNode(node, "wrong number of tensors");
  for (int index : node.in)
    if (index < 0) return failNode(node, "missing tensor");
  return true;
}

bool Loader::options(Node& node, int type) {
  if (node.optionsType != 0 && node.optionsType != type) return failNode(node, "wrong options");
  if (node.optionsType == 0) node.options = {};
  return true;
}

bool Loader::perTensor(const Node& node, const Tensor& t, const char* what) {
  if (t.scales.size() != 1 || t.zeroPoints.size() != 1)
    return failNode(node, std::string(what) + " needs one scale and zero point");
  const int32_t lowest = t.type == kUInt8 ? 0 : -128, highest = t.type == kUInt8 ? 255 : 127;
  if (t.zeroPoint() < lowest || t.zeroPoint() > highest)
    return failNode(node, std::string(what) + " zero point");
  return true;
}

bool Loader::activation(Node& node, int function, const Tensor& out) {
  const int32_t lowest = out.type == kUInt8 ? 0 : -128;
  const int32_t highest = out.type == kUInt8 ? 255 : 127;
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
    default: return failNode(node, "unsupported fused activation " + std::to_string(function));
  }
  if (!ok || node.actMin > node.actMax) return failNode(node, "bad activation range");
  return true;
}

bool Loader::steps(const Node& node) {
  constexpr int most = 1024;
  return node.strideW >= 1 && node.strideH >= 1 && node.dilationW >= 1 && node.dilationH >= 1 &&
         node.strideW <= most && node.strideH <= most && node.dilationW <= most && node.dilationH <= most;
}

void Loader::padding(int same, int stride, int dilation, int in, int filter, int& out, int& pad) {
  const int64_t effective = int64_t{filter - 1} * dilation + 1;
  const int64_t size =
    same == 0 ? (int64_t{in} + stride - 1) / stride : (int64_t{in} + stride - effective) / stride;
  out = static_cast<int>(std::max<int64_t>(size, 0));
  const int64_t total = std::max<int64_t>((int64_t{out} - 1) * stride + effective - in, 0);
  pad = static_cast<int>(total / 2);
}

int Loader::variableFor(std::string_view container, std::string_view name) {
  std::string key(container);
  key += '\n';
  key += name;
  for (std::size_t i = 0; i < variables.size(); ++i)
    if (variables[i].key == key) return static_cast<int>(i);
  variables.push_back({key});
  return static_cast<int>(variables.size() - 1);
}

bool Loader::prepare() {
  for (std::size_t s = 0; s < graphs_.size(); ++s) {
    WakeModel::Graph& g = graphs_[s];
    for (int index : g.inputs) {
      Tensor& t = g.tensors[index];
      if (t.constant || t.type == kResource || t.type < 0 || t.producer != -2) return fail("bad graph input");
      t.producer = -1;
    }
    for (std::size_t n = 0; n < g.nodes.size(); ++n) {
      Node& node = g.nodes[n];
      for (int index : node.in) {
        if (index < 0) continue;
        Tensor& t = g.tensors[index];
        if (!t.constant && t.producer == -2) return failNode(node, "reads a tensor nothing writes");
        t.lastUse = static_cast<int>(n);
      }
      for (int index : node.out) {
        Tensor& t = g.tensors[index];
        if (t.constant || t.producer != -2) return failNode(node, "writes a constant or a written tensor");
        if (t.type < 0) return failNode(node, "unsupported tensor type");
        t.producer = static_cast<int>(n);
      }
      if (!prepareNode(g, node)) return false;
    }
    for (int index : g.outputs) {
      Tensor& t = g.tensors[index];
      if (t.producer == -2 || t.constant || t.type == kResource) return fail("bad graph output");
      t.lastUse = static_cast<int>(g.nodes.size());
    }
  }

  const WakeModel::Graph& main = graphs_[0];
  if (main.inputs.size() != 1 || main.outputs.size() != 1)
    return fail("the model needs one input and one output");
  for (std::size_t s = 1; s < graphs_.size(); ++s) {
    if (!graphs_[s].inputs.empty() || !graphs_[s].outputs.empty()) return fail("an init subgraph has inputs");
  }
  std::vector<int> calls(graphs_.size(), 0);
  for (std::size_t s = 0; s < graphs_.size(); ++s)
    for (const Node& node : graphs_[s].nodes)
      if (node.code == kCallOnce) {
        if (s != 0) return fail("CALL_ONCE outside the main graph");
        ++calls[node.subgraph];
      }
  for (std::size_t s = 1; s < graphs_.size(); ++s)
    if (calls[s] != 1) return fail("every subgraph needs one CALL_ONCE");
  return checkVariables() && simulate(0, false);
}

bool Loader::prepareNode(WakeModel::Graph& g, Node& node) {
  switch (node.code) {
    case kConv: return conv(g, node);
    case kDepthwise: return depthwise(g, node);
    case kFullyConnected: return fullyConnected(g, node);
    case kAveragePool:
    case kMaxPool: return pool(g, node);
    case kAdd:
    case kMul: return elementwise(g, node);
    case kLogistic: return logistic(g, node);
    case kQuantize: return quantize(g, node);
    case kConcatenation: return concatenation(g, node);
    case kReshape: return reshape(g, node);
    case kStridedSlice: return stridedSlice(g, node);
    case kSplitV: return splitV(g, node);
    case kMean: return mean(g, node);
    case kVarHandle:
    case kReadVariable:
    case kAssignVariable:
    case kCallOnce: return variableNode(g, node);
    default: return fail("unsupported operator " + opName(node.code));
  }
}

bool Loader::conv(WakeModel::Graph& g, Node& node) {
  if (node.in.size() != 2 && node.in.size() != 3) return failNode(node, "wrong number of tensors");
  if (node.out.size() != 1) return failNode(node, "wrong number of tensors");
  if (!options(node, 1)) return false;
  if (node.in[0] < 0 || node.in[1] < 0) return failNode(node, "missing tensor");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& filter = g.tensors[node.in[1]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != kInt8 || filter.type != kInt8 || out.type != kInt8 || !filter.constant)
    return failNode(node, "needs int8 tensors and constant weights");
  if (in.rank != 4 || filter.rank != 4 || out.rank != 4) return failNode(node, "needs 4D tensors");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  const int same = flat_.field<int8_t>(node.options, 0, 0);
  node.strideW = flat_.field<int32_t>(node.options, 1, 0);
  node.strideH = flat_.field<int32_t>(node.options, 2, 0);
  const int function = flat_.field<int8_t>(node.options, 3, 0);
  node.dilationW = flat_.field<int32_t>(node.options, 4, 1);
  node.dilationH = flat_.field<int32_t>(node.options, 5, 1);
  if (same != 0 && same != 1) return failNode(node, "bad padding");
  if (!steps(node)) return failNode(node, "bad stride or dilation");
  const int outC = filter.dim(0), kC = filter.dim(3), inC = in.dim(3);
  if (inC % kC != 0) return failNode(node, "filter depth does not divide the input depth");
  node.groups = inC / kC;
  if (outC % node.groups != 0) return failNode(node, "bad groups");
  int outH = 0, outW = 0;
  padding(same, node.strideH, node.dilationH, in.dim(1), filter.dim(1), outH, node.padH);
  padding(same, node.strideW, node.dilationW, in.dim(2), filter.dim(2), outW, node.padW);
  if (out.dim(0) != in.dim(0) || out.dim(1) != outH || out.dim(2) != outW || out.dim(3) != outC)
    return failNode(node, "output shape does not match");
  if (int64_t{filter.dim(1)} * filter.dim(2) * kC > kMaxAccumulation)
    return failNode(node, "filter too large");
  if (filter.scales.size() != 1 && filter.scales.size() != static_cast<std::size_t>(outC))
    return failNode(node, "bad filter quantization");
  if (filter.scales.size() > 1 && filter.quantDim != 0) return failNode(node, "bad filter quantization");
  for (int64_t zero : filter.zeroPoints)
    if (zero != 0) return failNode(node, "filter zero points must be 0");
  if (node.in.size() == 3 && node.in[2] >= 0) {
    const Tensor& bias = g.tensors[node.in[2]];
    if (bias.type != kInt32 || !bias.constant || bias.elements != static_cast<std::size_t>(outC))
      return failNode(node, "bad bias");
    node.bias.resize(bias.elements);
    std::memcpy(node.bias.data(), bias.constant, bias.bytes);
  }
  node.inOffset = -in.zeroPoint();
  node.outOffset = out.zeroPoint();
  node.channelScales.resize(outC);
  for (int c = 0; c < outC; ++c) {
    const double scale = static_cast<double>(filter.scales.size() > 1 ? filter.scales[c] : filter.scales[0]);
    const double effective = static_cast<double>(in.scale()) * scale / static_cast<double>(out.scale());
    if (!quantizeMultiplier(effective, node.channelScales[c])) return failNode(node, "bad output scale");
  }
  return activation(node, function, out);
}

bool Loader::depthwise(WakeModel::Graph& g, Node& node) {
  if (node.in.size() != 2 && node.in.size() != 3) return failNode(node, "wrong number of tensors");
  if (node.out.size() != 1) return failNode(node, "wrong number of tensors");
  if (!options(node, 2)) return false;
  if (node.in[0] < 0 || node.in[1] < 0) return failNode(node, "missing tensor");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& filter = g.tensors[node.in[1]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != kInt8 || filter.type != kInt8 || out.type != kInt8 || !filter.constant)
    return failNode(node, "needs int8 tensors and constant weights");
  if (in.rank != 4 || filter.rank != 4 || out.rank != 4 || filter.dim(0) != 1)
    return failNode(node, "needs 4D tensors");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  const int same = flat_.field<int8_t>(node.options, 0, 0);
  node.strideW = flat_.field<int32_t>(node.options, 1, 0);
  node.strideH = flat_.field<int32_t>(node.options, 2, 0);
  const int function = flat_.field<int8_t>(node.options, 4, 0);
  node.dilationW = flat_.field<int32_t>(node.options, 5, 1);
  node.dilationH = flat_.field<int32_t>(node.options, 6, 1);
  if (same != 0 && same != 1) return failNode(node, "bad padding");
  if (!steps(node)) return failNode(node, "bad stride or dilation");
  const int outC = filter.dim(3), inC = in.dim(3);
  if (outC % inC != 0) return failNode(node, "filter channels do not match the input");
  node.depthMultiplier = outC / inC;
  int outH = 0, outW = 0;
  padding(same, node.strideH, node.dilationH, in.dim(1), filter.dim(1), outH, node.padH);
  padding(same, node.strideW, node.dilationW, in.dim(2), filter.dim(2), outW, node.padW);
  if (out.dim(0) != in.dim(0) || out.dim(1) != outH || out.dim(2) != outW || out.dim(3) != outC)
    return failNode(node, "output shape does not match");
  if (int64_t{filter.dim(1)} * filter.dim(2) > kMaxAccumulation) return failNode(node, "filter too large");
  if (filter.scales.size() != 1 && filter.scales.size() != static_cast<std::size_t>(outC))
    return failNode(node, "bad filter quantization");
  if (filter.scales.size() > 1 && filter.quantDim != 3) return failNode(node, "bad filter quantization");
  for (int64_t zero : filter.zeroPoints)
    if (zero != 0) return failNode(node, "filter zero points must be 0");
  if (node.in.size() == 3 && node.in[2] >= 0) {
    const Tensor& bias = g.tensors[node.in[2]];
    if (bias.type != kInt32 || !bias.constant || bias.elements != static_cast<std::size_t>(outC))
      return failNode(node, "bad bias");
    node.bias.resize(bias.elements);
    std::memcpy(node.bias.data(), bias.constant, bias.bytes);
  }
  node.inOffset = -in.zeroPoint();
  node.outOffset = out.zeroPoint();
  node.channelScales.resize(outC);
  for (int c = 0; c < outC; ++c) {
    const double scale = static_cast<double>(filter.scales.size() > 1 ? filter.scales[c] : filter.scales[0]);
    const double effective = static_cast<double>(in.scale()) * scale / static_cast<double>(out.scale());
    if (!quantizeMultiplier(effective, node.channelScales[c])) return failNode(node, "bad output scale");
  }
  return activation(node, function, out);
}

bool Loader::fullyConnected(WakeModel::Graph& g, Node& node) {
  if (node.in.size() != 2 && node.in.size() != 3) return failNode(node, "wrong number of tensors");
  if (node.out.size() != 1) return failNode(node, "wrong number of tensors");
  if (!options(node, 8)) return false;
  if (node.in[0] < 0 || node.in[1] < 0) return failNode(node, "missing tensor");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& filter = g.tensors[node.in[1]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != kInt8 || filter.type != kInt8 || out.type != kInt8 || !filter.constant || filter.rank != 2)
    return failNode(node, "needs int8 tensors and constant 2D weights");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  const int function = flat_.field<int8_t>(node.options, 0, 0);
  const int format = flat_.field<int8_t>(node.options, 1, 0);
  const bool keepDims = flat_.field<uint8_t>(node.options, 2, 0) != 0;
  if (format != 0) return failNode(node, "unsupported weights format");
  node.units = filter.dim(0);
  const std::size_t depth = static_cast<std::size_t>(filter.dim(1));
  if (in.elements % depth != 0) return failNode(node, "input does not match the weights");
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
  if (!shapeOk) return failNode(node, "output shape does not match");
  if (static_cast<int64_t>(depth) > kMaxAccumulation) return failNode(node, "weights too large");
  const bool perChannel = filter.scales.size() > 1;
  if (filter.scales.empty() || (perChannel && (filter.scales.size() != static_cast<std::size_t>(node.units) ||
                                               filter.quantDim != 0)))
    return failNode(node, "bad weight quantization");
  if (perChannel) {
    for (int64_t zero : filter.zeroPoints)
      if (zero != 0) return failNode(node, "weight zero points must be 0");
  } else {
    if (filter.zeroPoints.size() != 1 || filter.zeroPoint() < -128 || filter.zeroPoint() > 127)
      return failNode(node, "bad weight zero point");
    node.filterOffset = -filter.zeroPoint();
  }
  if (node.in.size() == 3 && node.in[2] >= 0) {
    const Tensor& bias = g.tensors[node.in[2]];
    if (bias.type != kInt32 || !bias.constant || bias.elements != static_cast<std::size_t>(node.units))
      return failNode(node, "bad bias");
    if (!perChannel) {
      const double product = static_cast<double>(in.scale()) * static_cast<double>(filter.scale());
      if (std::abs(product - static_cast<double>(bias.scale())) / static_cast<double>(out.scale()) > 0.02)
        return failNode(node, "bias scale does not match");
    }
    node.bias.resize(bias.elements);
    std::memcpy(node.bias.data(), bias.constant, bias.bytes);
  }
  node.inOffset = -in.zeroPoint();
  node.outOffset = out.zeroPoint();
  for (float scale : filter.scales) {
    const double effective =
      static_cast<double>(in.scale()) * static_cast<double>(scale) / static_cast<double>(out.scale());
    if (!std::isfinite(effective)) return failNode(node, "bad output scale");
    node.realScales.push_back(effective);
  }
  return activation(node, function, out);
}

bool Loader::pool(WakeModel::Graph& g, Node& node) {
  if (!count(node, 1, 1) || !options(node, 5)) return false;
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != kInt8 || out.type != kInt8 || in.rank != 4 || out.rank != 4)
    return failNode(node, "needs 4D int8 tensors");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  if (std::abs(in.scale() - out.scale()) > 1.0e-6f || in.zeroPoint() != out.zeroPoint())
    return failNode(node, "input and output quantization differ");
  const int same = flat_.field<int8_t>(node.options, 0, 0);
  node.strideW = flat_.field<int32_t>(node.options, 1, 0);
  node.strideH = flat_.field<int32_t>(node.options, 2, 0);
  node.filterW = flat_.field<int32_t>(node.options, 3, 0);
  node.filterH = flat_.field<int32_t>(node.options, 4, 0);
  const int function = flat_.field<int8_t>(node.options, 5, 0);
  if (same != 0 && same != 1) return failNode(node, "bad padding");
  if (node.strideW < 1 || node.strideH < 1 || node.filterW < 1 || node.filterH < 1 || node.strideW > 1024 ||
      node.strideH > 1024 || int64_t{node.filterW} * node.filterH > kMaxAccumulation)
    return failNode(node, "bad stride or filter");
  int outH = 0, outW = 0;
  padding(same, node.strideH, 1, in.dim(1), node.filterH, outH, node.padH);
  padding(same, node.strideW, 1, in.dim(2), node.filterW, outW, node.padW);
  if (out.dim(0) != in.dim(0) || out.dim(1) != outH || out.dim(2) != outW || out.dim(3) != in.dim(3))
    return failNode(node, "output shape does not match");
  for (int y = 0; y < outH; ++y) {
    const int origin = y * node.strideH - node.padH;
    if (std::min(node.filterH, in.dim(1) - origin) <= std::max(0, -origin))
      return failNode(node, "empty window");
  }
  for (int x = 0; x < outW; ++x) {
    const int origin = x * node.strideW - node.padW;
    if (std::min(node.filterW, in.dim(2) - origin) <= std::max(0, -origin))
      return failNode(node, "empty window");
  }
  return activation(node, function, out);
}

bool Loader::elementwise(WakeModel::Graph& g, Node& node) {
  const bool add = node.code == kAdd;
  if (!count(node, 2, 1) || !options(node, add ? 11 : 21)) return false;
  if (node.in[0] < 0 || node.in[1] < 0) return failNode(node, "missing tensor");
  const Tensor& a = g.tensors[node.in[0]];
  const Tensor& b = g.tensors[node.in[1]];
  const Tensor& out = g.tensors[node.out[0]];
  if (a.type != kInt8 || b.type != kInt8 || out.type != kInt8) return failNode(node, "needs int8 tensors");
  if (!perTensor(node, a, "input") || !perTensor(node, b, "input") || !perTensor(node, out, "output"))
    return false;
  node.shape1 = extend(a);
  node.shape2 = extend(b);
  node.shapeOut = extend(out);
  if (out.rank != std::max(a.rank, b.rank)) return failNode(node, "output shape does not match");
  for (std::size_t i = 0; i < kMaxRank; ++i) {
    const int32_t x = node.shape1.dims[i], y = node.shape2.dims[i];
    if (x != y && x != 1 && y != 1) return failNode(node, "shapes do not broadcast");
    if (node.shapeOut.dims[i] != std::max(x, y)) return failNode(node, "output shape does not match");
  }
  node.inOffset = -a.zeroPoint();
  node.in2Offset = -b.zeroPoint();
  node.outOffset = out.zeroPoint();
  if (add) {
    node.leftShift = 20;
    const double twiceMax = 2 * static_cast<double>(std::max(a.scale(), b.scale()));
    const double real1 = static_cast<double>(a.scale()) / twiceMax;
    const double real2 = static_cast<double>(b.scale()) / twiceMax;
    const double realOut =
      twiceMax / (static_cast<double>(1 << node.leftShift) * static_cast<double>(out.scale()));
    if (!(realOut > 0.0 && realOut < 1.0) || !quantizeMultiplier(real1, node.scale1) ||
        !quantizeMultiplier(real2, node.scale2) || !quantizeMultiplier(realOut, node.scaleOut) ||
        node.scale1.shift > 0 || node.scale2.shift > 0 || node.scaleOut.shift > 0)
      return failNode(node, "scales out of range");
  } else {
    const double real =
      static_cast<double>(a.scale()) * static_cast<double>(b.scale()) / static_cast<double>(out.scale());
    if (!quantizeMultiplier(real, node.scaleOut)) return failNode(node, "scales out of range");
  }
  return activation(node, flat_.field<int8_t>(node.options, 0, 0), out);
}

bool Loader::logistic(WakeModel::Graph& g, Node& node) {
  if (!count(node, 1, 1)) return false;
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != kInt8 || out.type != kInt8 || in.elements != out.elements)
    return failNode(node, "needs int8 tensors");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  if (static_cast<double>(out.scale()) != 1.0 / 256) return failNode(node, "output scale must be 1/256");
  const float inverse = 1 / out.scale();
  for (int32_t value = -128; value <= 127; ++value) {
    const float real = in.scale() * static_cast<float>(value - in.zeroPoint());
    const float transformed = 1.0f / (1.0f + std::exp(-real));
    const float rescaled = std::round(transformed * inverse);
    const int32_t quantized = static_cast<int32_t>(rescaled + static_cast<float>(out.zeroPoint()));
    node.lut[static_cast<uint8_t>(value)] = static_cast<int8_t>(std::max(-128, std::min(127, quantized)));
  }
  return true;
}

bool Loader::quantize(WakeModel::Graph& g, Node& node) {
  if (!count(node, 1, 1)) return false;
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& out = g.tensors[node.out[0]];
  if ((in.type != kInt8 && in.type != kUInt8) || (out.type != kInt8 && out.type != kUInt8) ||
      in.elements != out.elements)
    return failNode(node, "needs int8 or uint8 tensors");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  node.inOffset = in.zeroPoint();
  node.outOffset = out.zeroPoint();
  node.actMin = out.type == kUInt8 ? 0 : -128;
  node.actMax = out.type == kUInt8 ? 255 : 127;
  const double real = static_cast<double>(in.scale()) / static_cast<double>(out.scale());
  if (!quantizeMultiplier(real, node.scaleOut)) return failNode(node, "scales out of range");
  return true;
}

bool Loader::concatenation(WakeModel::Graph& g, Node& node) {
  if (node.in.empty() || node.out.size() != 1 || !options(node, 10))
    return failNode(node, "wrong number of tensors");
  const Tensor& out = g.tensors[node.out[0]];
  int axis = flat_.field<int32_t>(node.options, 0, 0);
  if (flat_.field<int8_t>(node.options, 1, 0) != 0) return failNode(node, "fused activation");
  if (axis < 0) axis += static_cast<int>(out.rank);
  if (axis < 0 || axis >= static_cast<int>(out.rank)) return failNode(node, "bad axis");
  int64_t total = 0;
  for (int index : node.in) {
    if (index < 0) return failNode(node, "missing tensor");
    const Tensor& t = g.tensors[index];
    if (t.type != out.type || t.rank != out.rank || t.type < 0 || t.type == kResource)
      return failNode(node, "inputs do not match");
    if (out.type == kInt8 && (!perTensor(node, t, "input") || !perTensor(node, out, "output") ||
                              t.scale() != out.scale() || t.zeroPoint() != out.zeroPoint()))
      return failNode(node, "int8 inputs need the output quantization");
    for (std::size_t d = 0; d < out.rank; ++d)
      if (static_cast<int>(d) != axis && t.dims[d] != out.dims[d])
        return failNode(node, "inputs do not match");
    total += t.dims[axis];
  }
  if (total != out.dims[axis]) return failNode(node, "output shape does not match");
  const std::size_t width = out.bytes / out.elements;
  node.outer = 1;
  for (int d = 0; d < axis; ++d) node.outer *= static_cast<std::size_t>(out.dims[d]);
  node.inner = width;
  for (std::size_t d = axis + 1; d < out.rank; ++d) node.inner *= static_cast<std::size_t>(out.dims[d]);
  for (int index : node.in)
    node.parts.push_back(static_cast<std::size_t>(g.tensors[index].dims[axis]) * node.inner);
  return true;
}

bool Loader::reshape(WakeModel::Graph& g, Node& node) {
  if (node.in.empty() || node.in.size() > 2 || node.out.size() != 1 || node.in[0] < 0)
    return failNode(node, "wrong number of tensors");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != out.type || in.elements != out.elements || in.type == kResource)
    return failNode(node, "output does not match the input");
  return true;
}

bool Loader::stridedSlice(WakeModel::Graph& g, Node& node) {
  if (!count(node, 4, 1) || !options(node, 32)) return false;
  for (int index : node.in)
    if (index < 0) return failNode(node, "missing tensor");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != out.type || in.type < 0 || in.type == kResource)
    return failNode(node, "output does not match");
  const int begins = flat_.field<int32_t>(node.options, 0, 0);
  const int ends = flat_.field<int32_t>(node.options, 1, 0);
  const int shrinks = flat_.field<int32_t>(node.options, 4, 0);
  if (flat_.field<int32_t>(node.options, 2, 0) != 0 || flat_.field<int32_t>(node.options, 3, 0) != 0 ||
      flat_.field<uint8_t>(node.options, 5, 0) != 0)
    return failNode(node, "ellipsis, new axis and offset are not supported");
  for (std::size_t i = 1; i < 4; ++i) {
    const Tensor& t = g.tensors[node.in[i]];
    if (t.type != kInt32 || !t.constant || t.rank != 1 || t.elements != in.rank)
      return failNode(node, "begin, end and strides must be constant int32 of the input rank");
  }
  const Tensor& begin = g.tensors[node.in[1]];
  const Tensor& end = g.tensors[node.in[2]];
  const Tensor& strides = g.tensors[node.in[3]];
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
    if (stride == 0 || stride < -kMaxDim || stride > kMaxDim) return failNode(node, "bad stride");
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
      if (first < 0 || first >= size) return failNode(node, "shrunk index out of range");
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
      if (elements < 1) return failNode(node, "empty slice");
      expected[kept++] = static_cast<int32_t>(elements);
    }
    node.start[pad + axis] = static_cast<int32_t>(first);
    node.step[pad + axis] = static_cast<int32_t>(stride);
    node.count[pad + axis] = static_cast<int32_t>(elements);
  }
  if (out.rank != kept) return failNode(node, "output shape does not match");
  for (std::size_t i = 0; i < kept; ++i)
    if (out.dims[i] != expected[i]) return failNode(node, "output shape does not match");
  node.shape1 = extend(in);
  return true;
}

bool Loader::splitV(WakeModel::Graph& g, Node& node) {
  if (node.in.size() != 3 || node.out.empty() || !options(node, 79))
    return failNode(node, "wrong number of tensors");
  for (int index : node.in)
    if (index < 0) return failNode(node, "missing tensor");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& sizes = g.tensors[node.in[1]];
  const Tensor& axisTensor = g.tensors[node.in[2]];
  if (in.type < 0 || in.type == kResource) return failNode(node, "unsupported input");
  if (flat_.field<int32_t>(node.options, 0, 0) != static_cast<int32_t>(node.out.size()))
    return failNode(node, "num_splits does not match the outputs");
  if (sizes.type != kInt32 || !sizes.constant || sizes.rank != 1 || sizes.elements != node.out.size())
    return failNode(node, "size_splits must be constant int32");
  if (axisTensor.type != kInt32 || !axisTensor.constant || axisTensor.elements != 1)
    return failNode(node, "axis must be a constant int32");
  int32_t axis = 0;
  std::memcpy(&axis, axisTensor.constant, 4);
  if (axis < 0) axis += static_cast<int32_t>(in.rank);
  if (axis < 0 || axis >= static_cast<int32_t>(in.rank)) return failNode(node, "bad axis");
  std::vector<int64_t> parts(node.out.size());
  int64_t known = 0;
  int unknown = -1;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    int32_t size = 0;
    std::memcpy(&size, sizes.constant + 4 * i, 4);
    if (size == -1 && unknown < 0) {
      unknown = static_cast<int>(i);
    } else if (size < 0) {
      return failNode(node, "bad size_splits");
    } else {
      known += size;
    }
    parts[i] = size;
  }
  if (unknown >= 0) parts[unknown] = in.dims[axis] - known;
  int64_t total = 0;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const Tensor& out = g.tensors[node.out[i]];
    if (parts[i] < 1 || out.type != in.type || out.rank != in.rank)
      return failNode(node, "output shape does not match");
    for (std::size_t d = 0; d < in.rank; ++d) {
      const int64_t want = static_cast<int32_t>(d) == axis ? parts[i] : in.dims[d];
      if (out.dims[d] != want) return failNode(node, "output shape does not match");
    }
    total += parts[i];
  }
  if (total != in.dims[axis]) return failNode(node, "size_splits do not add up");
  const std::size_t width = in.bytes / in.elements;
  node.outer = 1;
  for (int32_t d = 0; d < axis; ++d) node.outer *= static_cast<std::size_t>(in.dims[d]);
  node.inner = width;
  for (std::size_t d = axis + 1; d < in.rank; ++d) node.inner *= static_cast<std::size_t>(in.dims[d]);
  for (int64_t part : parts) node.parts.push_back(static_cast<std::size_t>(part) * node.inner);
  return true;
}

bool Loader::mean(WakeModel::Graph& g, Node& node) {
  if (!count(node, 2, 1) || !options(node, 27)) return false;
  if (node.in[1] < 0) return failNode(node, "missing tensor");
  const Tensor& in = g.tensors[node.in[0]];
  const Tensor& axes = g.tensors[node.in[1]];
  const Tensor& out = g.tensors[node.out[0]];
  if (in.type != kInt8 || out.type != kInt8) return failNode(node, "needs int8 tensors");
  if (!perTensor(node, in, "input") || !perTensor(node, out, "output")) return false;
  if (axes.type != kInt32 || !axes.constant || axes.rank > 1 || axes.elements > kMaxRank)
    return failNode(node, "axis must be constant int32");
  const bool keepDims = flat_.field<uint8_t>(node.options, 0, 0) != 0;
  const std::size_t pad = kMaxRank - in.rank;
  node.reduced.fill(false);
  for (std::size_t i = 0; i < axes.elements; ++i) {
    int32_t axis = 0;
    std::memcpy(&axis, axes.constant + 4 * i, 4);
    if (axis < 0) axis += static_cast<int32_t>(in.rank);
    if (axis < 0 || axis >= static_cast<int32_t>(in.rank)) return failNode(node, "bad axis");
    node.reduced[pad + axis] = true;
  }
  std::array<int32_t, kMaxRank> expected{};
  std::size_t rank = 0;
  node.reducedCount = 1;
  for (std::size_t d = 0; d < in.rank; ++d) {
    if (node.reduced[pad + d]) {
      node.reducedCount *= in.dims[d];
      if (keepDims) expected[rank++] = 1;
    } else {
      expected[rank++] = in.dims[d];
    }
  }
  if (out.rank != rank) return failNode(node, "output shape does not match");
  for (std::size_t d = 0; d < rank; ++d)
    if (out.dims[d] != expected[d]) return failNode(node, "output shape does not match");
  if (node.reducedCount > (int64_t{1} << 23)) return failNode(node, "too many values to average");
  Scale base;
  const double real = static_cast<double>(in.scale()) / static_cast<double>(out.scale());
  if (!quantizeMultiplier(real, base)) return failNode(node, "scales out of range");
  int shift = 63 - __builtin_clzll(static_cast<uint64_t>(node.reducedCount));
  shift = std::min(shift, 32);
  shift = std::min(shift, 31 + base.shift);
  node.scaleOut.multiplier =
    static_cast<int32_t>((static_cast<int64_t>(base.multiplier) << shift) / node.reducedCount);
  node.scaleOut.shift = base.shift - shift;
  if (node.scaleOut.shift < -31) return failNode(node, "scales out of range");
  node.inOffset = in.zeroPoint();
  node.outOffset = out.zeroPoint();
  node.shape1 = extend(in);
  node.sums.assign(out.elements, 0);
  return true;
}

bool Loader::variableNode(WakeModel::Graph& g, Node& node) {
  switch (node.code) {
    case kVarHandle: {
      if (!count(node, 0, 1) || !options(node, 111)) return false;
      Tensor& handle = g.tensors[node.out[0]];
      if (handle.type != kResource) return failNode(node, "needs a resource tensor");
      handle.variable = variableFor(flat_.string(node.options, 0), flat_.string(node.options, 1));
      return flat_.ok() || failNode(node, "malformed options");
    }
    case kReadVariable:
    case kAssignVariable: {
      const bool read = node.code == kReadVariable;
      if (!count(node, read ? 1 : 2, read ? 1 : 0)) return false;
      if (node.in[0] < 0 || (!read && node.in[1] < 0)) return failNode(node, "missing tensor");
      const Tensor& handle = g.tensors[node.in[0]];
      const Tensor& value = g.tensors[read ? node.out[0] : node.in[1]];
      if (handle.type != kResource || handle.variable < 0) return failNode(node, "needs a variable handle");
      if (value.type < 0 || value.type == kResource) return failNode(node, "unsupported value type");
      Variable& v = variables[handle.variable];
      if (v.type >= 0 && (v.type != value.type || v.bytes != value.bytes))
        return failNode(node, "the variable changes its shape");
      v.type = value.type;
      v.bytes = value.bytes;
      return true;
    }
    default: {
      if (!count(node, 0, 0) || !options(node, 103)) return false;
      node.subgraph = flat_.field<int32_t>(node.options, 0, 0);
      if (node.subgraph < 1 || static_cast<std::size_t>(node.subgraph) >= graphs_.size())
        return failNode(node, "bad init subgraph");
      return true;
    }
  }
}

bool Loader::checkVariables() {
  for (const Variable& v : variables)
    if (v.type < 0) return fail("a variable is never read or written");
  return true;
}

// Walks the first invoke in execution order: every variable is written before it is read.
bool Loader::simulate(std::size_t graph, bool nested) {
  for (const Node& node : graphs_[graph].nodes) {
    if (node.code == kCallOnce) {
      if (nested) return fail("nested CALL_ONCE");
      if (!simulate(static_cast<std::size_t>(node.subgraph), true)) return false;
    } else if (node.code == kReadVariable) {
      if (!variables[graphs_[graph].tensors[node.in[0]].variable].assigned)
        return fail("READ_VARIABLE before the variable is set");
    } else if (node.code == kAssignVariable) {
      variables[graphs_[graph].tensors[node.in[0]].variable].assigned = true;
    }
  }
  return true;
}

// Greedy by size: each tensor takes the lowest aligned offset clear of every tensor alive with it.
bool Loader::plan(std::vector<uint8_t>& arena) {
  std::size_t total = 0;
  std::vector<std::size_t> regions;
  for (WakeModel::Graph& g : graphs_) {
    std::vector<int> order;
    for (std::size_t i = 0; i < g.tensors.size(); ++i) {
      Tensor& t = g.tensors[i];
      t.needsMemory = !t.constant && t.type != kResource && t.producer != -2 && t.bytes > 0;
      if (t.needsMemory) {
        if (t.lastUse < t.producer) t.lastUse = t.producer;
        order.push_back(static_cast<int>(i));
      }
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return g.tensors[a].bytes > g.tensors[b].bytes; });
    std::vector<int> placed;
    std::size_t size = 0;
    for (int index : order) {
      Tensor& t = g.tensors[index];
      std::vector<std::pair<std::size_t, std::size_t>> busy;
      for (int other : placed) {
        const Tensor& o = g.tensors[other];
        if (o.producer <= t.lastUse && t.producer <= o.lastUse)
          busy.push_back({o.offset, o.offset + o.bytes});
      }
      std::sort(busy.begin(), busy.end());
      std::size_t offset = 0;
      for (const auto& range : busy) {
        if (offset + t.bytes <= range.first) break;
        offset = std::max(offset, (range.second + kAlign - 1) / kAlign * kAlign);
      }
      t.offset = offset;
      size = std::max(size, offset + t.bytes);
      placed.push_back(index);
    }
    regions.push_back(total);
    total += (size + kAlign - 1) / kAlign * kAlign;
    if (total > kMaxArena) return fail("the model needs too much memory");
  }
  for (Variable& v : variables) {
    v.offset = total;
    total += (v.bytes + kAlign - 1) / kAlign * kAlign;
    if (total > kMaxArena) return fail("the model needs too much memory");
  }
  arena.assign(total, 0);
  for (std::size_t s = 0; s < graphs_.size(); ++s) {
    for (Tensor& t : graphs_[s].tensors) {
      if (t.constant) {
        t.data = const_cast<uint8_t*>(t.constant);
      } else if (t.type == kResource && t.variable >= 0) {
        t.data = arena.data() + variables[t.variable].offset;
      } else if (t.needsMemory) {
        t.data = arena.data() + regions[s] + t.offset;
      }
    }
  }
  return true;
}

int32_t clampTo(int32_t value, const Node& n) { return std::min(n.actMax, std::max(n.actMin, value)); }

void convKernel(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& f = ts[n.in[1]];
  const Tensor& out = ts[n.out[0]];
  const int inH = in.dim(1), inW = in.dim(2), inC = in.dim(3);
  const int outH = out.dim(1), outW = out.dim(2), outC = out.dim(3);
  const int kH = f.dim(1), kW = f.dim(2), kC = f.dim(3);
  const int perGroup = outC / n.groups;
  const int8_t* input = in.i8();
  const int8_t* filter = f.i8();
  int8_t* output = out.o8();
  for (int b = 0; b < in.dim(0); ++b) {
    for (int oy = 0; oy < outH; ++oy) {
      for (int ox = 0; ox < outW; ++ox) {
        for (int oc = 0; oc < outC; ++oc) {
          const int group = oc / perGroup;
          int32_t acc = 0;
          for (int fy = 0; fy < kH; ++fy) {
            const int iy = oy * n.strideH - n.padH + n.dilationH * fy;
            if (iy < 0 || iy >= inH) continue;
            for (int fx = 0; fx < kW; ++fx) {
              const int ix = ox * n.strideW - n.padW + n.dilationW * fx;
              if (ix < 0 || ix >= inW) continue;
              const int8_t* x =
                input + ((static_cast<std::size_t>(b) * inH + iy) * inW + ix) * inC + group * kC;
              const int8_t* w = filter + ((static_cast<std::size_t>(oc) * kH + fy) * kW + fx) * kC;
              for (int c = 0; c < kC; ++c) acc += int32_t{w[c]} * (int32_t{x[c]} + n.inOffset);
            }
          }
          if (!n.bias.empty()) acc = wrapAdd(acc, n.bias[oc]);
          acc = wrapAdd(multiply(acc, n.channelScales[oc]), n.outOffset);
          output[((static_cast<std::size_t>(b) * outH + oy) * outW + ox) * outC + oc] =
            static_cast<int8_t>(clampTo(acc, n));
        }
      }
    }
  }
}

void depthwiseKernel(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& f = ts[n.in[1]];
  const Tensor& out = ts[n.out[0]];
  const int inH = in.dim(1), inW = in.dim(2), inC = in.dim(3);
  const int outH = out.dim(1), outW = out.dim(2), outC = out.dim(3);
  const int kH = f.dim(1), kW = f.dim(2);
  const int8_t* input = in.i8();
  const int8_t* filter = f.i8();
  int8_t* output = out.o8();
  for (int b = 0; b < in.dim(0); ++b) {
    for (int oy = 0; oy < outH; ++oy) {
      for (int ox = 0; ox < outW; ++ox) {
        for (int ic = 0; ic < inC; ++ic) {
          for (int m = 0; m < n.depthMultiplier; ++m) {
            const int oc = m + ic * n.depthMultiplier;
            int32_t acc = 0;
            for (int fy = 0; fy < kH; ++fy) {
              const int iy = oy * n.strideH - n.padH + n.dilationH * fy;
              if (iy < 0 || iy >= inH) continue;
              for (int fx = 0; fx < kW; ++fx) {
                const int ix = ox * n.strideW - n.padW + n.dilationW * fx;
                if (ix < 0 || ix >= inW) continue;
                const int32_t x = input[((static_cast<std::size_t>(b) * inH + iy) * inW + ix) * inC + ic];
                const int32_t w = filter[(static_cast<std::size_t>(fy) * kW + fx) * outC + oc];
                acc += w * (x + n.inOffset);
              }
            }
            if (!n.bias.empty()) acc = wrapAdd(acc, n.bias[oc]);
            acc = wrapAdd(multiply(acc, n.channelScales[oc]), n.outOffset);
            output[((static_cast<std::size_t>(b) * outH + oy) * outW + ox) * outC + oc] =
              static_cast<int8_t>(clampTo(acc, n));
          }
        }
      }
    }
  }
}

void fullyConnectedKernel(const std::vector<Tensor>& ts, const Node& n) {
  const int8_t* input = ts[n.in[0]].i8();
  const int8_t* weights = ts[n.in[1]].i8();
  int8_t* output = ts[n.out[0]].o8();
  const bool perChannel = n.realScales.size() > 1;
  for (std::size_t b = 0; b < n.outer; ++b) {
    const int8_t* x = input + b * n.inner;
    for (int u = 0; u < n.units; ++u) {
      const int8_t* w = weights + static_cast<std::size_t>(u) * n.inner;
      int32_t acc = 0;
      for (std::size_t d = 0; d < n.inner; ++d)
        acc += (int32_t{w[d]} + n.filterOffset) * (int32_t{x[d]} + n.inOffset);
      if (!n.bias.empty()) acc = wrapAdd(acc, n.bias[u]);
      double scaled = std::round(static_cast<double>(acc) * n.realScales[perChannel ? u : 0]);
      scaled = std::max(-2147483648.0, std::min(2147483647.0, scaled));
      const int32_t value = wrapAdd(static_cast<int32_t>(scaled), n.outOffset);
      output[b * n.units + u] = static_cast<int8_t>(clampTo(value, n));
    }
  }
}

void poolKernel(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& out = ts[n.out[0]];
  const int inH = in.dim(1), inW = in.dim(2), depth = in.dim(3);
  const int8_t* input = in.i8();
  int8_t* output = out.o8();
  const bool average = n.code == kAveragePool;
  for (int b = 0; b < in.dim(0); ++b) {
    for (int oy = 0; oy < out.dim(1); ++oy) {
      for (int ox = 0; ox < out.dim(2); ++ox) {
        const int x0 = ox * n.strideW - n.padW, y0 = oy * n.strideH - n.padH;
        const int fxStart = std::max(0, -x0), fxEnd = std::min(n.filterW, inW - x0);
        const int fyStart = std::max(0, -y0), fyEnd = std::min(n.filterH, inH - y0);
        for (int c = 0; c < depth; ++c) {
          int32_t acc = 0, count = 0;
          int8_t peak = std::numeric_limits<int8_t>::lowest();
          for (int fy = fyStart; fy < fyEnd; ++fy) {
            for (int fx = fxStart; fx < fxEnd; ++fx) {
              const std::size_t at = (static_cast<std::size_t>(b) * inH + y0 + fy) * inW + x0 + fx;
              const int8_t v = input[at * depth + c];
              acc += v;
              peak = std::max(peak, v);
              ++count;
            }
          }
          int32_t value = peak;
          if (average) value = acc > 0 ? (acc + count / 2) / count : (acc - count / 2) / count;
          output[((static_cast<std::size_t>(b) * out.dim(1) + oy) * out.dim(2) + ox) * depth + c] =
            static_cast<int8_t>(clampTo(value, n));
        }
      }
    }
  }
}

void elementwiseKernel(const std::vector<Tensor>& ts, const Node& n) {
  const int8_t* a = ts[n.in[0]].i8();
  const int8_t* b = ts[n.in[1]].i8();
  int8_t* out = ts[n.out[0]].o8();
  const auto sa = stridesOf(n.shape1), sb = stridesOf(n.shape2);
  std::array<std::size_t, kMaxRank> stepA{}, stepB{};
  for (std::size_t i = 0; i < kMaxRank; ++i) {
    stepA[i] = n.shape1.dims[i] == 1 ? 0 : sa[i];
    stepB[i] = n.shape2.dims[i] == 1 ? 0 : sb[i];
  }
  const bool add = n.code == kAdd;
  const auto& d = n.shapeOut.dims;
  std::size_t o = 0;
  for (int i0 = 0; i0 < d[0]; ++i0)
    for (int i1 = 0; i1 < d[1]; ++i1)
      for (int i2 = 0; i2 < d[2]; ++i2)
        for (int i3 = 0; i3 < d[3]; ++i3)
          for (int i4 = 0; i4 < d[4]; ++i4)
            for (int i5 = 0; i5 < d[5]; ++i5) {
              const std::size_t ia = i0 * stepA[0] + i1 * stepA[1] + i2 * stepA[2] + i3 * stepA[3] +
                                     i4 * stepA[4] + i5 * stepA[5];
              const std::size_t ib = i0 * stepB[0] + i1 * stepB[1] + i2 * stepB[2] + i3 * stepB[3] +
                                     i4 * stepB[4] + i5 * stepB[5];
              const int32_t x = n.inOffset + a[ia], y = n.in2Offset + b[ib];
              int32_t value = 0;
              if (add) {
                const int32_t sum =
                  multiply(x * (1 << n.leftShift), n.scale1) + multiply(y * (1 << n.leftShift), n.scale2);
                value = wrapAdd(multiply(sum, n.scaleOut), n.outOffset);
              } else {
                value = wrapAdd(n.outOffset, multiply(x * y, n.scaleOut));
              }
              out[o++] = static_cast<int8_t>(clampTo(value, n));
            }
}

void stridedSliceKernel(const std::vector<Tensor>& ts, const Node& n) {
  const uint8_t* in = ts[n.in[0]].data;
  uint8_t* out = ts[n.out[0]].data;
  const std::size_t width = ts[n.in[0]].bytes / ts[n.in[0]].elements;
  const auto s = stridesOf(n.shape1);
  std::size_t o = 0;
  for (int32_t a = 0; a < n.count[0]; ++a)
    for (int32_t b = 0; b < n.count[1]; ++b)
      for (int32_t c = 0; c < n.count[2]; ++c)
        for (int32_t d = 0; d < n.count[3]; ++d)
          for (int32_t e = 0; e < n.count[4]; ++e)
            for (int32_t f = 0; f < n.count[5]; ++f) {
              const std::array<int32_t, kMaxRank> at = {a, b, c, d, e, f};
              std::size_t i = 0;
              for (std::size_t k = 0; k < kMaxRank; ++k)
                i += static_cast<std::size_t>(n.start[k] + at[k] * n.step[k]) * s[k];
              std::memcpy(out + o, in + i * width, width);
              o += width;
            }
}

void meanKernel(const std::vector<Tensor>& ts, Node& n) {
  const int8_t* in = ts[n.in[0]].i8();
  int8_t* out = ts[n.out[0]].o8();
  const auto& d = n.shape1.dims;
  std::array<std::size_t, kMaxRank> outStep{};
  std::size_t step = 1;
  for (std::size_t k = kMaxRank; k-- > 0;) {
    outStep[k] = n.reduced[k] ? 0 : step;
    if (!n.reduced[k]) step *= static_cast<std::size_t>(d[k]);
  }
  std::fill(n.sums.begin(), n.sums.end(), 0);
  std::size_t i = 0;
  for (int i0 = 0; i0 < d[0]; ++i0)
    for (int i1 = 0; i1 < d[1]; ++i1)
      for (int i2 = 0; i2 < d[2]; ++i2)
        for (int i3 = 0; i3 < d[3]; ++i3)
          for (int i4 = 0; i4 < d[4]; ++i4)
            for (int i5 = 0; i5 < d[5]; ++i5) {
              const std::size_t o = i0 * outStep[0] + i1 * outStep[1] + i2 * outStep[2] + i3 * outStep[3] +
                                    i4 * outStep[4] + i5 * outStep[5];
              n.sums[o] += in[i++];
            }
  for (std::size_t o = 0; o < n.sums.size(); ++o) {
    const int32_t shifted = static_cast<int32_t>(n.sums[o] - int64_t{n.inOffset} * n.reducedCount);
    const int32_t value = multiply(shifted, n.scaleOut) + n.outOffset;
    out[o] = static_cast<int8_t>(std::min(127, std::max(-128, value)));
  }
}

}

WakeModel::~WakeModel() = default;

std::unique_ptr<WakeModel> WakeModel::load(const uint8_t* data, std::size_t size, std::string& error) {
  error.clear();
  if (!data || size < 8 || size > (std::size_t{64} << 20)) {
    error = "not a TensorFlow Lite model";
    return nullptr;
  }
  std::unique_ptr<WakeModel> model(new WakeModel());
  model->file_.assign(data, data + size);
  Loader loader(model->file_, model->graphs_, error);
  if (!loader.parse() || !loader.prepare() || !loader.plan(model->arena_)) return nullptr;

  const Graph& main = model->graphs_[0];
  const Tensor& in = main.tensors[main.inputs[0]];
  const Tensor& out = main.tensors[main.outputs[0]];
  if (in.type != kInt8 || in.rank != 3 || in.dims[0] != 1 || in.dims[2] != static_cast<int32_t>(kFeatures) ||
      in.scales.size() != 1 || in.zeroPoints.size() != 1) {
    error = "the input must be int8 [1, N, 40]";
    return nullptr;
  }
  if (out.type != kInt8 && out.type != kUInt8) {
    error = "the output must be int8 or uint8";
    return nullptr;
  }
  model->frames_ = static_cast<std::size_t>(in.dims[1]);
  model->inputScale_ = in.scale();
  model->inputZeroPoint_ = in.zeroPoint();
  model->input_ = in.o8();
  model->output_ = out.data;
  model->outputSize_ = out.bytes;
  model->outputUint8_ = out.type == kUInt8;
  model->called_.assign(model->graphs_.size(), 0);
  return model;
}

void WakeModel::reset() { std::fill(called_.begin(), called_.end(), 0); }

uint8_t WakeModel::invoke() {
  run(0);
  return output_[0];
}

void WakeModel::run(std::size_t graph) {
  Graph& g = graphs_[graph];
  const std::vector<Tensor>& ts = g.tensors;
  for (Node& n : g.nodes) {
    switch (n.code) {
      case kConv: convKernel(ts, n); break;
      case kDepthwise: depthwiseKernel(ts, n); break;
      case kFullyConnected: fullyConnectedKernel(ts, n); break;
      case kAveragePool:
      case kMaxPool: poolKernel(ts, n); break;
      case kAdd:
      case kMul: elementwiseKernel(ts, n); break;
      case kLogistic: {
        const Tensor& in = ts[n.in[0]];
        int8_t* out = ts[n.out[0]].o8();
        for (std::size_t i = 0; i < in.elements; ++i) out[i] = n.lut[static_cast<uint8_t>(in.i8()[i])];
        break;
      }
      case kQuantize: {
        const Tensor& in = ts[n.in[0]];
        const Tensor& out = ts[n.out[0]];
        for (std::size_t i = 0; i < in.elements; ++i) {
          const int32_t x = in.type == kUInt8 ? int32_t{in.data[i]} : int32_t{in.i8()[i]};
          const int32_t value = clampTo(multiply(x - n.inOffset, n.scaleOut) + n.outOffset, n);
          out.data[i] = static_cast<uint8_t>(value);
        }
        break;
      }
      case kConcatenation: {
        uint8_t* out = ts[n.out[0]].data;
        for (std::size_t k = 0; k < n.outer; ++k) {
          for (std::size_t i = 0; i < n.in.size(); ++i) {
            std::memcpy(out, ts[n.in[i]].data + k * n.parts[i], n.parts[i]);
            out += n.parts[i];
          }
        }
        break;
      }
      case kSplitV: {
        const uint8_t* in = ts[n.in[0]].data;
        for (std::size_t k = 0; k < n.outer; ++k) {
          for (std::size_t i = 0; i < n.out.size(); ++i) {
            std::memcpy(ts[n.out[i]].data + k * n.parts[i], in, n.parts[i]);
            in += n.parts[i];
          }
        }
        break;
      }
      case kReshape: std::memcpy(ts[n.out[0]].data, ts[n.in[0]].data, ts[n.out[0]].bytes); break;
      case kStridedSlice: stridedSliceKernel(ts, n); break;
      case kMean: meanKernel(ts, n); break;
      case kReadVariable: std::memcpy(ts[n.out[0]].data, ts[n.in[0]].data, ts[n.out[0]].bytes); break;
      case kAssignVariable: std::memcpy(ts[n.in[0]].data, ts[n.in[1]].data, ts[n.in[1]].bytes); break;
      case kCallOnce:
        if (!called_[n.subgraph]) {
          called_[n.subgraph] = 1;
          run(static_cast<std::size_t>(n.subgraph));
        }
        break;
      default: break;
    }
  }
}

}
