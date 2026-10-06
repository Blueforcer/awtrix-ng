#pragma once

#include <string>

namespace awtrix::host::persistence {

// Durable host writes of persisted documents, retried on failure.
enum class Document { Settings, DeviceConfig, AppOrder, Radio };

// Attempts an atomic durable write immediately. On failure, retains the newest
// value for this document for retry. False means accepted in memory, not durable.
// The data directory is configured once at startup, before any saves.
bool save(Document document, const std::string& json);
bool pending(Document document);
bool hasPending();

// Retry all retained writes; return true only if none remain. Call periodically
// and at shutdown. Callers must not report a clean shutdown while this is false.
bool flushPending();

}
