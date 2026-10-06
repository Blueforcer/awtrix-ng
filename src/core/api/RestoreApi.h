#pragma once

#include "core/api/ApiRouter.h"
#include "core/backup/RestoreApplier.h"
#include "core/backup/ZipReader.h"

namespace awtrix::api {

// Complete the actual streaming parser and release an unfinished entry on parser failure.
// The applier and its sink must remain alive until this function returns.
backup::RestoreResult finishRestore(backup::ZipReader& reader, backup::RestoreApplier& applier);

HttpResult restoreResponse(const backup::RestoreResult& result, bool persistencePending = false);
bool restoreChangedAssets(const backup::RestoreResult& result);

}
