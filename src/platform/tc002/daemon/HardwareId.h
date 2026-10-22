#pragma once

#include <array>
#include <cstdint>
#include <string>

// The clock's activation id (RuntimeContract.h kActivationFlag), the one value the count of TC002
// clocks rests on, and with it a planned licence with Ulanzi. The ZKSWE flash driver reports a 16-byte id unique to each clock;
// the activation id is the lowercase hex SHA-256 of kActivationLabel followed by those 16 bytes.
// The raw id never leaves the daemon.
namespace awtrix {
namespace tc002d {

using UniqueId = std::array<std::uint8_t, 16>;

inline constexpr char kActivationLabel[] = "AWTRIX-NG-TC002-ACTIVATION-1";

// Asks the driver at device for the id: the unlock request with its fixed key, then the read.
// False with the reason in error when the driver is missing or answers anything but 16 bytes.
bool readUniqueId(const std::string& device, UniqueId& id, std::string& error);

// Empty for an id of only 0x00 or only 0xFF bytes, which no clock reports.
std::string activationId(const UniqueId& id);

}
}
