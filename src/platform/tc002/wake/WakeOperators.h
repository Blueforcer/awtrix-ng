#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "platform/tc002/wake/WakeGraph.h"

namespace awtrix::tc002::wake {

struct Variable {
  std::string key;
  int type = -1;
  std::size_t bytes = 0;
  bool assigned = false;
  std::size_t offset = 0;
};

bool prepareOperator(Flat& flat, WakeModel::Graph& g, Node& node, std::size_t graphCount,
                     std::vector<Variable>& variables);

}
