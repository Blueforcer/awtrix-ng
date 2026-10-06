#pragma once

#include <cstdint>

namespace awtrix_test {

// Advances this test process's monotonic clock without waiting.
void advanceClock(int64_t milliseconds);

}
