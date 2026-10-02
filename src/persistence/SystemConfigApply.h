#pragma once

#include <string>

#include "persistence/DeviceConfig.h"

namespace awtrix {
namespace sysconfig {


struct ApplyError {
  int status = 400;
  std::string code;
  std::string message;
  std::string field;
};

enum class Origin { Interactive, Restore };

// On a board with fixed wiring, replaces the pins and the panel size with the board's own, keeping
// a panel height the board allows (else its tallest). Run on the stored config at boot, so a pin
// map or panel stored by another build cannot make apply() refuse every later edit.
void adoptFixedBoard(DeviceConfig& cfg, const pins::SocProfile& soc = pins::activeProfile());

// Validates obj against the cross-field rules and merges it into cfg, reporting how many fields
// were taken. On failure cfg is left exactly as it was and err carries the HTTP status.
// soc is the board's pin and panel rules; tests pass another board's.
bool apply(DeviceConfig& cfg, api::JsonReader obj, int& applied, ApplyError& err,
           Origin origin = Origin::Interactive,
           const pins::SocProfile& soc = pins::activeProfile());

}
}
