// The tensor checks follow TensorFlow Lite (tensorflow/lite/kernels, Copyright The TensorFlow Authors, Apache-2.0).
#include "platform/tc002/wake/WakeLoader.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "platform/tc002/wake/WakeOperators.h"

namespace awtrix::tc002::wake {
namespace {

constexpr std::size_t kMaxGraphs = 8;
constexpr std::size_t kMaxTensors = 4096;
constexpr std::size_t kMaxNodes = 1024;
constexpr std::size_t kMaxElements = std::size_t{1} << 22;
constexpr std::size_t kMaxArena = std::size_t{64} << 20;
constexpr std::size_t kAlign = 16;

constexpr const char* kBadGraph = "unsupported graph";
constexpr const char* kTooLarge = "the model needs too much memory";

constexpr std::size_t alignUp(std::size_t size) { return (size + kAlign - 1) / kAlign * kAlign; }

bool planArena(std::vector<WakeModel::Graph>& graphs, std::vector<Variable>& variables, std::vector<uint8_t>& arena) {
  std::size_t total = 0;
  std::vector<std::size_t> regions;
  for (WakeModel::Graph& g : graphs) {
    std::vector<int> order;
    for (std::size_t i = 0; i < g.tensors.size(); ++i) {
      Tensor& t = g.tensors[i];
      t.needsMemory = !t.constant && t.type != kResource && t.producer != kNoProducer && t.bytes > 0;
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
        offset = std::max(offset, alignUp(range.second));
      }
      t.offset = offset;
      size = std::max(size, offset + t.bytes);
      placed.push_back(index);
    }
    regions.push_back(total);
    total += alignUp(size);
    if (total > kMaxArena) return false;
  }
  for (Variable& v : variables) {
    v.offset = total;
    total += alignUp(v.bytes);
    if (total > kMaxArena) return false;
  }
  arena.assign(total, 0);
  for (std::size_t s = 0; s < graphs.size(); ++s) {
    for (Tensor& t : graphs[s].tensors) {
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

class Loader {
 public:
  Loader(const uint8_t* file, std::size_t size, std::vector<WakeModel::Graph>& graphs, std::string& error)
      : flat_(file, size), graphs_(graphs), error_(error) {}

  bool parse();
  bool prepare();
  bool plan(std::vector<uint8_t>& arena) { return planArena(graphs_, variables_, arena) || fail(kTooLarge); }

 private:
  bool fail(std::string message) {
    error_ = std::move(message);
    return false;
  }
  bool unsupported(const Node& node) { return fail("unsupported operator " + std::to_string(node.code)); }
  bool parseTensor(const Flat::Table& t, Tensor& out);
  bool checkVariables();
  bool simulate(std::size_t graph);

  Flat flat_;
  std::vector<WakeModel::Graph>& graphs_;
  std::string& error_;
  std::vector<int> codes_;
  Flat::Vector buffers_;
  std::vector<Variable> variables_;
};

bool Loader::parse() {
  const Flat::Table model = flat_.root();
  const uint32_t version = flat_.field<uint32_t>(model, 0, 0);
  const Flat::Vector codes = flat_.vector(model, 1, 4);
  const Flat::Vector subgraphs = flat_.vector(model, 2, 4);
  buffers_ = flat_.vector(model, 4, 4);
  if (!flat_.ok() || !model.present || version != 3 || subgraphs.count == 0 || subgraphs.count > kMaxGraphs ||
      codes.count > 256)
    return fail(kNotAModel);

  for (std::size_t i = 0; i < codes.count; ++i) {
    const Flat::Table code = flat_.tableIn(codes, i);
    const int deprecated = flat_.field<int8_t>(code, 0, 0);
    const int builtin = flat_.field<int32_t>(code, 3, 0);
    codes_.push_back(std::max(deprecated, builtin));
  }

  graphs_.resize(subgraphs.count);
  for (std::size_t s = 0; s < subgraphs.count; ++s) {
    WakeModel::Graph& g = graphs_[s];
    const Flat::Table sub = flat_.tableIn(subgraphs, s);
    const Flat::Vector tensors = flat_.vector(sub, 0, 4);
    const Flat::Vector inputs = flat_.vector(sub, 1, 4);
    const Flat::Vector outputs = flat_.vector(sub, 2, 4);
    const Flat::Vector nodes = flat_.vector(sub, 3, 4);
    if (!flat_.ok() || tensors.count > kMaxTensors || nodes.count > kMaxNodes) return fail(kNotAModel);
    g.tensors.resize(tensors.count);
    for (std::size_t t = 0; t < tensors.count; ++t) {
      if (!parseTensor(flat_.tableIn(tensors, t), g.tensors[t])) return fail("unsupported tensor " + std::to_string(t));
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
    if (!indices(inputs, g.inputs, false) || !indices(outputs, g.outputs, false)) return fail(kNotAModel);
    g.nodes.resize(nodes.count);
    for (std::size_t n = 0; n < nodes.count; ++n) {
      Node& node = g.nodes[n];
      const Flat::Table op = flat_.tableIn(nodes, n);
      const uint32_t opcode = flat_.field<uint32_t>(op, 0, 0);
      if (!flat_.ok() || opcode >= codes_.size()) return fail(kNotAModel);
      node.code = codes_[opcode];
      if (!indices(flat_.vector(op, 1, 4), node.in, true) || !indices(flat_.vector(op, 2, 4), node.out, false))
        return fail(kNotAModel);
      node.optionsType = flat_.field<uint8_t>(op, 3, 0);
      node.options = flat_.table(op, 4);
      if (flat_.field<uint8_t>(op, 11, 0) != 0) return unsupported(node);
      if (!flat_.ok()) return fail(kNotAModel);
    }
  }
  return true;
}

bool Loader::parseTensor(const Flat::Table& t, Tensor& out) {
  const Flat::Vector shape = flat_.vector(t, 0, 4);
  out.type = flat_.field<int8_t>(t, 1, 0);
  const uint32_t buffer = flat_.field<uint32_t>(t, 2, 0);
  const Flat::Table quant = flat_.table(t, 4);
  if (flat_.table(t, 6).present || flat_.field<uint32_t>(t, 10, 0) != 0 || !flat_.ok() || shape.count > kMaxRank)
    return false;
  out.rank = shape.count;
  for (std::size_t i = 0; i < shape.count; ++i) {
    const int32_t dim = flat_.element<int32_t>(shape, i);
    if (dim < 1 || dim > kMaxDim) return false;
    out.dims[i] = dim;
    out.elements *= static_cast<std::size_t>(dim);
    if (out.elements > kMaxElements) return false;
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
    if (flat_.field<uint8_t>(quant, 4, 0) != 0 || !flat_.ok() || scales.count > 4096 || zeros.count > 4096)
      return false;
    for (std::size_t i = 0; i < scales.count; ++i) {
      const float s = flat_.element<float>(scales, i);
      if (!std::isfinite(s) || s <= 0.0f) return false;
      out.scales.push_back(s);
    }
    for (std::size_t i = 0; i < zeros.count; ++i) out.zeroPoints.push_back(flat_.element<int64_t>(zeros, i));
    if (!out.scales.empty() && out.zeroPoints.size() != out.scales.size() && out.zeroPoints.size() > 1)
      return false;
    for (int64_t zero : out.zeroPoints)
      if (zero < -32768 || zero > 65535) return false;
  }

  if (buffer != 0) {
    if (buffer >= buffers_.count) return false;
    const Flat::Table b = flat_.tableIn(buffers_, buffer);
    const Flat::Vector data = flat_.vector(b, 0, 1);
    const uint64_t offset = flat_.field<uint64_t>(b, 1, 0);
    const uint64_t size = flat_.field<uint64_t>(b, 2, 0);
    if (!flat_.ok()) return false;
    if (data.count > 0) {
      if (data.count != out.bytes) return false;
      out.constant = flat_.at(data.pos);
    } else if (offset > 1) {
      if (size != out.bytes || !flat_.contains(offset, size)) return false;
      out.constant = flat_.at(static_cast<std::size_t>(offset));
    }
    if (out.constant && (out.type < 0 || out.type == kResource || out.bytes == 0))
      return false;
  }
  return true;
}

bool Loader::prepare() {
  for (std::size_t s = 0; s < graphs_.size(); ++s) {
    WakeModel::Graph& g = graphs_[s];
    for (int index : g.inputs) {
      Tensor& t = g.tensors[index];
      if (t.constant || t.type == kResource || t.type < 0 || t.producer != kNoProducer) return fail(kBadGraph);
      t.producer = kGraphInputProducer;
    }
    for (std::size_t n = 0; n < g.nodes.size(); ++n) {
      Node& node = g.nodes[n];
      for (int index : node.in) {
        if (index < 0) continue;
        Tensor& t = g.tensors[index];
        if (!t.constant && t.producer == kNoProducer) return unsupported(node);
        t.lastUse = static_cast<int>(n);
      }
      for (int index : node.out) {
        Tensor& t = g.tensors[index];
        if (t.constant || t.producer != kNoProducer || t.type < 0) return unsupported(node);
        t.producer = static_cast<int>(n);
      }
      if (!prepareOperator(flat_, g, node, graphs_.size(), variables_)) return unsupported(node);
    }
    for (int index : g.outputs) {
      Tensor& t = g.tensors[index];
      if (t.producer == kNoProducer || t.constant || t.type == kResource) return fail(kBadGraph);
      t.lastUse = static_cast<int>(g.nodes.size());
    }
  }

  const WakeModel::Graph& main = graphs_[0];
  if (main.inputs.size() != 1 || main.outputs.size() != 1) return fail(kBadGraph);
  for (std::size_t s = 1; s < graphs_.size(); ++s) {
    if (!graphs_[s].inputs.empty() || !graphs_[s].outputs.empty()) return fail(kBadGraph);
  }
  std::vector<int> calls(graphs_.size(), 0);
  for (std::size_t s = 0; s < graphs_.size(); ++s)
    for (const Node& node : graphs_[s].nodes)
      if (node.code == kCallOnce) {
        if (s != 0) return fail(kBadGraph);
        ++calls[node.subgraph];
      }
  for (std::size_t s = 1; s < graphs_.size(); ++s)
    if (calls[s] != 1) return fail(kBadGraph);
  return checkVariables() && simulate(0);
}

bool Loader::checkVariables() {
  for (const Variable& v : variables_)
    if (v.type < 0) return fail(kBadGraph);
  return true;
}

bool Loader::simulate(std::size_t graph) {
  for (const Node& node : graphs_[graph].nodes) {
    if (node.code == kCallOnce) {
      if (!simulate(static_cast<std::size_t>(node.subgraph))) return false;
    } else if (node.code == kReadVariable) {
      if (!variables_[graphs_[graph].tensors[node.in[0]].variable].assigned)
        return fail(kBadGraph);
    } else if (node.code == kAssignVariable) {
      variables_[graphs_[graph].tensors[node.in[0]].variable].assigned = true;
    }
  }
  return true;
}

}

bool loadGraphs(const uint8_t* file, std::size_t size, std::vector<WakeModel::Graph>& graphs,
                std::vector<uint8_t>& arena, std::string& error) {
  Loader loader(file, size, graphs, error);
  return loader.parse() && loader.prepare() && loader.plan(arena);
}

}
