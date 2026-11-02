#include "platform/tc002/wake/WakeModel.h"

#include <algorithm>

#include "platform/tc002/wake/WakeGraph.h"
#include "platform/tc002/wake/WakeKernels.h"
#include "platform/tc002/wake/WakeLoader.h"

namespace awtrix::tc002::wake {

WakeModel::~WakeModel() = default;

std::unique_ptr<WakeModel> WakeModel::load(const uint8_t* data, std::size_t size, std::string& error) {
  error.clear();
  if (!data || size < 8 || size > (std::size_t{64} << 20)) {
    error = kNotAModel;
    return nullptr;
  }
  std::unique_ptr<WakeModel> model(new WakeModel());
  model->file_.assign(data, data + size);
  if (!loadGraphs(model->file_, model->graphs_, model->arena_, error)) return nullptr;

  const Graph& main = model->graphs_[0];
  const Tensor& in = main.tensors[main.inputs[0]];
  const Tensor& out = main.tensors[main.outputs[0]];
  if (in.type != kInt8 || in.rank != 3 || in.dims[0] != 1 || in.dims[2] != static_cast<int32_t>(kFeatures) ||
      in.scales.size() != 1 || in.zeroPoints.size() != 1) {
    error = "the input must be int8 [1, N, 40]";
    return nullptr;
  }
  if (out.type != kUInt8 || out.bytes != 1) {
    error = "the output must be one uint8";
    return nullptr;
  }
  model->frames_ = static_cast<std::size_t>(in.dims[1]);
  model->inputScale_ = in.scale();
  model->inputZeroPoint_ = in.zeroPoint();
  model->input_ = in.o8();
  model->output_ = out.data;
  model->called_.assign(model->graphs_.size(), 0);
  return model;
}

void WakeModel::reset() { std::fill(called_.begin(), called_.end(), 0); }

uint8_t WakeModel::invoke() {
  run(0);
  return output_[0];
}

void WakeModel::run(std::size_t graph) {
  const Graph& g = graphs_[graph];
  const std::vector<Tensor>& ts = g.tensors;
  for (const Node& n : g.nodes) {
    switch (n.code) {
      case kConv: kernels::conv(ts, n); break;
      case kDepthwise: kernels::depthwise(ts, n); break;
      case kFullyConnected: kernels::fullyConnected(ts, n); break;
      case kAveragePool: kernels::averagePool(ts, n); break;
      case kLogistic: kernels::logistic(ts, n); break;
      case kQuantize: kernels::quantize(ts, n); break;
      case kConcatenation: kernels::concatenation(ts, n); break;
      case kSplitV: kernels::splitV(ts, n); break;
      case kStridedSlice: kernels::stridedSlice(ts, n); break;
      case kReshape:
      case kReadVariable: kernels::copyToOutput(ts, n); break;
      case kAssignVariable: kernels::assignVariable(ts, n); break;
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
