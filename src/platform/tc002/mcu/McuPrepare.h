#pragma once

namespace awtrix::tc002::mcu {
// Dedicated child mode: returns without initializing the runtime or opening hardware.
int prepare(int argc, char** argv);
}
