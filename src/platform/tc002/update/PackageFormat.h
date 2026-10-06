#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "platform/tc002/contract/ReleaseName.h"
#include "platform/tc002/update/PackageConstants.h"

// AWUPD003: a 68-byte header, target and release names, manifest SHA-256, then the payload, for
// awtrix-ng:tc002 the squashfs image of a release slot (release_slot.h). PackageVerifier.h checks
// both digests.
namespace awtrix {
namespace tc002 {
namespace update {

constexpr std::size_t kHeaderBytes = 68;
constexpr std::size_t kManifestDigestBytes = 32;
constexpr std::size_t kMaxTargetBytes = 128;
constexpr std::size_t kMaxManifestBytes = kHeaderBytes + kMaxTargetBytes + kMaxReleaseNameBytes;
// Limits of the format; a caller passes its own, lower payload limit.
constexpr std::uint64_t kMaxPayloadBytes = 256ULL << 20;

struct PackageHeader {
  std::string target;
  std::string release;
  // Lowercase hex SHA-256 of the payload.
  std::string payloadSha256;
  std::uint64_t counter = 0;
  // Header and names: the bytes the manifest digest covers.
  std::size_t manifestBytes = 0;
  std::uint64_t payloadOffset = 0;
  std::uint64_t payloadBytes = 0;

  std::uint64_t packageBytes() const { return payloadOffset + payloadBytes; }
};

// Why a manifest was refused: format, limit or digest.
struct FormatProblem {
  const char* code = "";
  std::string message;
};

struct CheckedManifest {
  PackageHeader header;
  std::array<unsigned char, kMaxManifestBytes + kManifestDigestBytes> bytes{};
};

// Reads and verifies the bounded manifest without changing the descriptor offset.
bool readCheckedManifest(int fd, std::uint64_t size, std::uint64_t maxPayloadBytes,
                         CheckedManifest& out, FormatProblem& problem);

bool productionTarget(std::string_view target);
// kProductionTarget, or "experimental:" and a name of [A-Za-z0-9._:-] up to kMaxTargetBytes.
bool allowedTarget(std::string_view target);

// From the first kHeaderBytes: how many manifest bytes to read (magic, version and sizes checked).
bool manifestSize(const unsigned char* header, std::size_t& manifestBytes, FormatProblem& problem);
// The complete manifest of manifestSize() bytes.
bool readManifest(const unsigned char* manifest, std::size_t size, std::uint64_t maxPayloadBytes, PackageHeader& out,
                  FormatProblem& problem);

}
}
}
