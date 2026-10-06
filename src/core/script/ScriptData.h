#pragma once

#include <string>

#include "core/script/ScriptConfig.h"

namespace awtrix::script {

// What a script wrote with store.set(): its store without the values of declared settings, which
// belong to /config, and without null values, which store.get() reads as never written.
bool appendDataJson(std::string& out, const ConfigSchema& schema, const std::string& storeJson);

// Merges patchJson into the store: a value replaces, null removes, keys the patch does not name
// keep their value. A declared setting is refused, so a setting's validation cannot be bypassed.
StorePatch applyDataPatch(const ConfigSchema& schema, const std::string& storeJson,
                          const std::string& patchJson);

int dataResponse(const std::string& name, const ConfigTextFn& readSource,
                 const ConfigTextFn& readStore, std::string& body);

}
