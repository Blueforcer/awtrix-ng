#pragma once

#include <vector>

#include "platform/tc002/wake/WakeGraph.h"

namespace awtrix::tc002::wake::kernels {

void conv(const std::vector<Tensor>& ts, const Node& n);
void depthwise(const std::vector<Tensor>& ts, const Node& n);
void fullyConnected(const std::vector<Tensor>& ts, const Node& n);
void averagePool(const std::vector<Tensor>& ts, const Node& n);
void logistic(const std::vector<Tensor>& ts, const Node& n);
void quantize(const std::vector<Tensor>& ts, const Node& n);
void concatenation(const std::vector<Tensor>& ts, const Node& n);
void splitV(const std::vector<Tensor>& ts, const Node& n);
void stridedSlice(const std::vector<Tensor>& ts, const Node& n);
void copyToOutput(const std::vector<Tensor>& ts, const Node& n);
void assignVariable(const std::vector<Tensor>& ts, const Node& n);

}
