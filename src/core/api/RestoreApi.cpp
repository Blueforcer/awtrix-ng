#include "core/api/RestoreApi.h"

namespace awtrix::api {

backup::RestoreResult finishRestore(backup::ZipReader& reader, backup::RestoreApplier& applier) {
  if (!reader.finish()) applier.abort(reader.error());
  return applier.result();
}

HttpResult restoreResponse(const backup::RestoreResult& result, bool persistencePending) {
  HttpResult response;
  if (result.ok && persistencePending) {
    response.status = 507;
    response.body = errorJson("insufficientStorage",
                             "restored, not saved yet");
  } else {
    response.status = result.ok ? 200 : 400;
    response.body = result.toJson();
  }
  return response;
}

bool restoreChangedAssets(const backup::RestoreResult& result) {
  return result.icons > 0 || result.melodies > 0 || result.palettes > 0 || result.mp3 > 0;
}

}
