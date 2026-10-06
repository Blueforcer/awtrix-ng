#include "core/api/ApiRouter.h"

#include "core/CoreEngine.h"
#include "core/api/StateJson.h"

namespace awtrix::api {

HttpResult commandResponse(CoreEngine& engine, const Command& cmd, DispatchResult result,
                           bool persistencePending) {
  if (result == DispatchResult::Ok) {
    if (persistencePending)
      return errorResult(507, "insufficientStorage", "applied, not saved yet");
    if (cmd.type == CommandType::SetSettings)
      return {200, "application/json", buildSettingsJson(engine)};
  }
  return httpResponse(cmd, result, engine.lastDetail());
}

}
