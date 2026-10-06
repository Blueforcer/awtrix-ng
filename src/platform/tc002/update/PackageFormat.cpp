#include "platform/tc002/update/PackageFormat.h"

#include <cstring>

#include "platform/posix/Files.h"
#include "platform/posix/sha256.h"

namespace awtrix {
namespace tc002 {
namespace update {
namespace {

constexpr unsigned char kMagic[8] = {'A', 'W', 'U', 'P', 'D', '0', '0', '3'};

std::uint64_t bigEndian(const unsigned char* data, std::size_t size) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < size; ++i) value = (value << 8) | data[i];
  return value;
}

bool identifier(std::string_view value) {
  if (value.empty() || value.size() > kMaxTargetBytes) return false;
  for (const unsigned char c : value)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
          c == '-' || c == ':'))
      return false;
  return true;
}

bool refuse(FormatProblem& problem, const char* code, const char* message) {
  problem.code = code;
  problem.message = message;
  return false;
}

}

bool productionTarget(std::string_view target) { return target == kProductionTarget; }

bool allowedTarget(std::string_view target) {
  constexpr std::string_view prefix = "experimental:";
  return productionTarget(target) ||
         (identifier(target) && target.size() > prefix.size() && target.substr(0, prefix.size()) == prefix);
}

bool readCheckedManifest(int fd, std::uint64_t size, std::uint64_t maxPayloadBytes,
                         CheckedManifest& out, FormatProblem& problem) {
  out = CheckedManifest{};
  auto* bytes = out.bytes.data();
  std::size_t manifestBytes = 0;
  if (size < kHeaderBytes + kManifestDigestBytes || !posix::preadAll(fd, 0, bytes, kHeaderBytes))
    return refuse(problem, "format", "package truncated or unreadable");
  if (!manifestSize(bytes, manifestBytes, problem)) return false;
  const auto prefixBytes = manifestBytes + kManifestDigestBytes;
  if (size < prefixBytes ||
      !posix::preadAll(fd, kHeaderBytes, bytes + kHeaderBytes, prefixBytes - kHeaderBytes))
    return refuse(problem, "format", "package truncated or unreadable");
  if (!readManifest(bytes, manifestBytes, maxPayloadBytes, out.header, problem)) return false;
  if (size != out.header.packageBytes())
    return refuse(problem, "format", "package length mismatch or trailing data");
  sha256_state hash;
  sha256_init(&hash);
  sha256_update(&hash, bytes, manifestBytes);
  unsigned char digest[kManifestDigestBytes];
  sha256_final(&hash, digest);
  if (std::memcmp(digest, bytes + manifestBytes, sizeof digest) != 0)
    return refuse(problem, "digest", "manifest digest mismatch");
  return true;
}

bool manifestSize(const unsigned char* header, std::size_t& manifestBytes, FormatProblem& problem) {
  if (std::memcmp(header, kMagic, sizeof kMagic) != 0 || bigEndian(header + 8, 2) != 3 ||
      bigEndian(header + 10, 2) != 0)
    return refuse(problem, "format", "unsupported package format");
  const std::uint64_t declared = bigEndian(header + 12, 4);
  const std::uint64_t targetBytes = bigEndian(header + 32, 2);
  const std::uint64_t releaseBytes = bigEndian(header + 34, 2);
  if (targetBytes == 0 || targetBytes > kMaxTargetBytes || releaseBytes == 0 || releaseBytes > kMaxReleaseNameBytes ||
      declared != kHeaderBytes + targetBytes + releaseBytes)
    return refuse(problem, "format", "invalid manifest bounds");
  manifestBytes = static_cast<std::size_t>(declared);
  return true;
}

bool readManifest(const unsigned char* manifest, std::size_t size, std::uint64_t maxPayloadBytes, PackageHeader& out,
                  FormatProblem& problem) {
  out = PackageHeader{};
  std::size_t expected = 0;
  if (size < kHeaderBytes || !manifestSize(manifest, expected, problem)) {
    if (size < kHeaderBytes) refuse(problem, "format", "package truncated or unreadable");
    return false;
  }
  if (size != expected) return refuse(problem, "format", "invalid manifest bounds");
  const std::size_t targetBytes = static_cast<std::size_t>(bigEndian(manifest + 32, 2));
  const std::size_t releaseBytes = static_cast<std::size_t>(bigEndian(manifest + 34, 2));
  out.manifestBytes = size;
  out.payloadOffset = size + kManifestDigestBytes;
  out.payloadBytes = bigEndian(manifest + 16, 8);
  out.counter = bigEndian(manifest + 24, 8);
  if (out.payloadBytes == 0 || out.payloadBytes > maxPayloadBytes || out.payloadBytes > kMaxPayloadBytes)
    return refuse(problem, "limit", "payload exceeds resource bounds");
  if (out.counter == 0) return refuse(problem, "format", "the release counter is zero");
  out.target.assign(reinterpret_cast<const char*>(manifest + kHeaderBytes), targetBytes);
  out.release.assign(reinterpret_cast<const char*>(manifest + kHeaderBytes + targetBytes), releaseBytes);
  if (!identifier(out.target)) return refuse(problem, "format", "invalid target identifier");
  if (!validReleaseName(out.release)) return refuse(problem, "format", "invalid release identifier");
  out.payloadSha256 = posix::hexBytes(manifest + 36, 32);
  return true;
}

}
}
}
