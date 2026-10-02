#pragma once

#include <cstddef>

namespace awtrix::script::heap {

// Largest block malloc can hand out on the Pico, as far as newlib lets anyone tell.
std::size_t picoLargestFreeBlock();

}
