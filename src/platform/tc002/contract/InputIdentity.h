#pragma once

namespace awtrix::tc002 {

enum class InputKind { Other, Keys, Knob };
InputKind inputKind(int fd);

}
