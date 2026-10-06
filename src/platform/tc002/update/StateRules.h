#pragma once

#include "platform/tc002/update/StateDocument.h"
#include "platform/tc002/update/PackageFormat.h"
#include "core/Sha256Hex.h"
#include "platform/tc002/contract/ReleaseName.h"

namespace awtrix::tc002::update::state_detail {

inline bool hasCandidate(State state) {
  return state == State::Staged || state == State::Activating ||
         state == State::BootPending || state == State::Quarantined;
}

// Lease owner identifiers.
inline bool identifier(const std::string& value, std::size_t maxSize) {
  if (value.empty() || value.size() > maxSize) return false;
  for (const unsigned char c : value) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ':'))
      return false;
  }
  return true;
}

inline bool validOwner(const std::string& owner) { return identifier(owner, kMaxOwnerBytes); }

inline bool printable(const std::string& value, std::size_t maxSize) {
  if (value.empty() || value.size() > maxSize) return false;
  for (const unsigned char c : value) if (c < 0x20 || c > 0x7e) return false;
  return true;
}

inline bool validReason(const std::string& reason) { return printable(reason, kMaxReasonBytes); }

inline bool validStagedFile(const std::string& path) {
  return !path.empty() && path.size() <= kMaxStagedFileBytes && path.find('\0') == std::string::npos;
}

inline bool validRelease(const ReleaseRecord& record) {
  return record.counter != 0 && isSha256Hex(record.payloadSha256) &&
         tc002::validReleaseName(record.release) && allowedTarget(record.target);
}

}
