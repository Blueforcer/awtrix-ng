#pragma once

#include <cstdint>
#include <string>

#include "platform/tc002/update/PackageFormat.h"

// Checks AWUPD003 manifest and payload SHA-256 digests with OpenSSL, and that the payload of an
// awtrix-ng:tc002 package is a release slot image.
namespace awtrix {
namespace tc002 {
namespace update {

struct Policy {
  std::string expectedTarget;  // kProductionTarget or "experimental:ID".
  std::uint64_t currentCounter = 0;
  std::uint64_t maxPayloadBytes = kMaxPayloadBytes;
};

// Stable failure classes for callers that answer users: "policy", "path", "format",
// "limit", "counter", "target", "digest", "stage".
struct Result {
  bool ok = false;
  std::string code;
  std::string error;
  std::string target;
  std::string release;
  std::string payloadSha256;
  std::uint64_t counter = 0;
  std::uint64_t payloadBytes = 0;
  std::string stagedFile;
};

// Verifies one regular file. If stageDirectory is set, copy the same verified
// bytes into an existing caller-owned 0700 directory and publish atomically,
// without replacing an existing file. Never extracts or installs the payload.
// An error after publication may leave a valid file with uncertain durability;
// callers must treat only ok=true as a durable staging acknowledgement.
Result verify(const std::string& package, const Policy& policy, const std::string& stageDirectory = {});

}
}
}
