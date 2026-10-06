#pragma once
#include "core/api/JsonReader.h"
#include "platform/linux/layout/Layout.h"

namespace awtrix::layout {
// Decodes the value of the top-level `layout` field. Unknown/duplicate fields are errors.
bool parse(api::JsonReader reader, LayoutSpec& out, DispatchDetail& error);
}
