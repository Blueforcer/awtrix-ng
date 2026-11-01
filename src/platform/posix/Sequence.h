#pragma once

#include <climits>

namespace awtrix::posix {

constexpr int nextInSequence(int value) { return value == INT_MAX ? 1 : value + 1; }

}
