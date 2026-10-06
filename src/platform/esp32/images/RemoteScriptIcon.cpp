#include "media/ScriptIcon.h"

namespace awtrix {

void ScriptIconSet::loadRemote(Entry& e, std::string_view, int64_t) {
  e.state = State::kMissing;
  e.retryStep = 0;
}

}
