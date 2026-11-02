#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/tc002/wake/WakeGraph.h"

namespace awtrix::tc002::wake {

inline constexpr char kNotAModel[] = "not a TensorFlow Lite model";

bool loadGraphs(const std::vector<uint8_t>& file, std::vector<WakeModel::Graph>& graphs, std::vector<uint8_t>& arena,
                std::string& error);

}
