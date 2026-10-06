#include "core/Sha256Hex.h"
#include "platform/posix/Bytes.h"
#include "core/payload/Crc.h"
#include "platform/tc002/daemon/McuFirmware.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <set>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "platform/posix/sha256.h"

namespace awtrix::tc002d::mcu {
bool readFirmwareFile(const std::string& path, std::string& text, std::size_t limit) {
  return posix::readRegularFile(AT_FDCWD, path, limit, text);
}
std::string firmwareHash(const std::string& data) {
  sha256_state sha;
  uint8_t digest[32]; char encoded[65];
  sha256_init(&sha); sha256_update(&sha, data.data(), data.size());
  sha256_final(&sha, digest); sha256_hex(digest, encoded);
  return encoded;
}
namespace {
bool object(const std::string& text) {
  api::JsonReader r(text);
  if (!api::isWellFormed(text) || !r.enterObject()) return false;
  std::set<std::string> keys;
  while (r.nextMember()) {
    if (!keys.insert(std::string(r.key())).second || !r.skipValue()) return false;
  }
  return r.ok();
}
bool number(api::JsonReader r, const char* name, uint32_t& out) {
  const auto field = api::memberValue(r, name);
  long long n = 0;
  if (!field.isInteger() || !field.asLong(n) || n < 0 || n > 0xffffffffLL) return false;
  out = static_cast<uint32_t>(n);
  return true;
}
bool identity(api::JsonReader r, Identity& i) {
  return number(r, "abi", i.abi) && number(r, "features", i.features) && number(r, "family", i.family) &&
         number(r, "version", i.version) && number(r, "build_tag", i.tag) &&
         i.abi == 1 && i.family == 123 && i.features <= 255 && i.version > 0;
}

}

bool Identity::decode(const uint8_t* p, std::size_t n, Identity& out) {
  if (n != 16 || std::memcmp(p, "AWMC", 4)) return false;
  out = {p[4], p[5], uint32_t(p[6]) | uint32_t(p[7]) << 8, posix::le32(p+8), posix::le32(p+12)};
  return out.abi && out.version;
}
bool Identity::sameBuild(const Identity& other) const {
  return abi == other.abi && features == other.features && family == other.family &&
         version == other.version && tag == other.tag;
}

void Firmware::load(const std::string& directory, const std::string& journal) {
  *this = Firmware{};
  journal_ = journal;
  std::string text;
  if (!journal.empty() && readFirmwareFile(journal, text, 4096)) {
    api::JsonReader r(text);
    const std::string phase = api::memberText(r, "phase");
    pendingDigest_ = api::memberText(r, "sha256");
    if (!object(text) || !identity(r, pendingTarget_) || !isSha256Hex(pendingDigest_) ||
        (phase != "attempt" && phase != "verified")) {
      failed("invalid MCU update journal"); return;
    }
    pending_ = phase == "attempt";
  } else if (!journal.empty() && errno != ENOENT) {
    failed("cannot read MCU update journal"); return;
  }
  if (directory.empty()) return;
  if (!readFirmwareFile(directory + "/manifest.json", text, 4096)) {
    if (errno != ENOENT) failed("cannot read MCU manifest");
    return;
  }
  api::JsonReader r(text);
  uint32_t schema = 0, bytes = 0;
  digest_ = api::memberText(r, "image_sha256");
  const auto extensionHash = api::memberText(r, "sha256");
  if (!object(text) || !number(r, "schema", schema) || schema != 2 || api::memberText(r, "target") != "tc002" ||
      api::memberText(r, "base_version") != "V1.0.17" || api::memberText(r, "file") != "extension.bin" ||
      api::memberText(r, "recipe") != "tc002-pcm-v1" || api::memberText(r, "base_sha256") != kBaseHash ||
      !identity(r, target_) || !number(r, "bytes", bytes) || bytes <= 256 || bytes > 4096 ||
      !number(r, "image_bytes", imageBytes_) || imageBytes_ < 512 || imageBytes_ > 262144 ||
      !isSha256Hex(digest_) || !isSha256Hex(extensionHash)) {
    failed("invalid MCU manifest"); return;
  }
  if (!readFirmwareFile(directory + "/extension.bin", extension_, 4096) || extension_.size() != bytes ||
      firmwareHash(extension_) != extensionHash) {
    failed("MCU extension size/digest mismatch"); return;
  }
  const auto marker = extension_.find("AWMC");
  Identity compiled;
  if (marker == std::string::npos || extension_.find("AWMC", marker+1) != std::string::npos ||
      extension_.size()-marker < 16 ||
      !Identity::decode(reinterpret_cast<const uint8_t*>(extension_.data()+marker), 16, compiled) ||
      !compiled.sameBuild(target_)) {
    failed("MCU extension identity mismatch"); return;
  }
  status_ = "probing";
}

bool Firmware::loadPrepared(const std::string& path) {
  std::string binary;
  if (!readFirmwareFile(path, binary, 262144)) {
    if (errno != ENOENT) failed("cannot read locally prepared MCU image");
    return false;
  }
  return acceptPrepared(binary);
}

bool Firmware::acceptPrepared(const std::string& binary) {
  if (!valid() || binary.size() != imageBytes_ || binary.size() < 512) {
    failed("local MCU image size mismatch"); return false;
  }
  const auto* b = reinterpret_cast<const uint8_t*>(binary.data());
  if (digest_ != firmwareHash(binary) || std::memcmp(b, "POT\0", 4) || posix::le32(b+4) != imageBytes_-8 ||
      std::memcmp(b+0x10, "XBOX\1\0\1\0", 8) ||
      crc16Ccitt(b, 0xfe) != (b[0xfe] | b[0xff] << 8) ||
      crc16Ccitt(b+0x100, 256) != (b[0xfc] | b[0xfd] << 8)) {
    failed("local MCU image digest/header mismatch"); return false;
  }
  image_.assign(b, b+binary.size());
  return true;
}

bool Firmware::record(const char* phase, const Identity& i, const std::string& digest) {
  std::string text;
  api::JsonWriter json(text);
  json.beginObject().member("phase", phase).member("abi", i.abi).member("features", i.features)
      .member("family", i.family).member("version", i.version).member("build_tag", i.tag)
      .member("sha256", digest).endObject();
  if (journal_.empty() || !posix::replaceText(journal_, text)) { failed("cannot persist MCU update journal"); return false; }
  return true;
}

bool Firmware::needed(const std::string& vendor, const Identity* current, bool malformed) {
  if (blocked_) return false;
  if (malformed) { status_ = "invalid MCU identity; update blocked"; return false; }
  if (pending_) {
    if (!current || !current->sameBuild(pendingTarget_)) {
      status_ = "previous MCU attempt unverified; automatic retry blocked";
      return false;
    }
    if (!record("verified", pendingTarget_, pendingDigest_)) return false;
    pending_ = false;
  }
  if (extension_.empty()) { status_ = "unbundled"; return false; }
  if (current) {
    if (current->abi != target_.abi || current->family != target_.family) {
      status_ = "incompatible MCU identity"; return false;
    }
    if (current->version >= target_.version) {
      if (current->version == target_.version && !current->sameBuild(target_)) status_ = "MCU version collision";
      else if ((current->features & target_.features) != target_.features) status_ = "MCU features incompatible";
      else status_ = current->version == target_.version ? "current" : "newer compatible MCU retained";
      return false;
    }
  } else if (vendor != "V1.0.17") { status_ = "unknown legacy MCU; update blocked"; return false; }
  status_ = "waiting for healthy AWTRIX and USB power";
  return true;
}

bool Firmware::recordAttempt() {
  if (blocked_ || pending_ || image_.empty() || !record("attempt", target_, digest_)) return false;
  pending_ = true;
  pendingTarget_ = target_;
  pendingDigest_ = digest_;
  status_ = "transferring";
  return true;
}
void Firmware::failed(const std::string& reason) { blocked_ = true; status_ = reason; }
}
