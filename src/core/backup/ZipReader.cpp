#include "core/payload/Crc.h"
#include "core/backup/ZipReader.h"

#include <algorithm>
#include <array>

namespace awtrix {
namespace backup {

namespace {

constexpr uint32_t kLocalSig = 0x04034b50u;
constexpr uint32_t kCentralSig = 0x02014b50u;
constexpr uint32_t kEocdSig = 0x06054b50u;
constexpr uint16_t kFlagDataDescriptor = 0x0008u;
constexpr std::size_t kFixedHeaderLen = 30;
constexpr uint16_t kMaxNameLen = 512;
constexpr uint16_t kMaxExtraLen = 4096;

uint16_t rd16(const std::string& b, std::size_t off) {
  return static_cast<uint16_t>(static_cast<uint8_t>(b[off])) |
         static_cast<uint16_t>(static_cast<uint8_t>(b[off + 1])) << 8;
}

uint32_t rd32(const std::string& b, std::size_t off) {
  return static_cast<uint32_t>(static_cast<uint8_t>(b[off])) |
         static_cast<uint32_t>(static_cast<uint8_t>(b[off + 1])) << 8 |
         static_cast<uint32_t>(static_cast<uint8_t>(b[off + 2])) << 16 |
         static_cast<uint32_t>(static_cast<uint8_t>(b[off + 3])) << 24;
}


}

ZipReader::ZipReader(ZipVisitor& visitor) : visitor_(visitor) {}

bool ZipReader::fail(const char* message) {
  ok_ = false;
  phase_ = Phase::End;
  error_ = message;
  return false;
}

// Offsets into the 30-byte fixed part of a local file header: 6 flags, 8 method, 14 crc32,
// 18 compressed size, 26 name length, 28 extra length.
void ZipReader::parseFixedHeader() {
  if (rd16(hdr_, 6) & kFlagDataDescriptor) {
    fail("data descriptors unsupported");
    return;
  }
  if (rd16(hdr_, 8) != 0) {
    fail("compressed entries unsupported");
    return;
  }
  expectedCrc_ = rd32(hdr_, 14);
  const uint32_t compSize = rd32(hdr_, 18);
  dataRemaining_ = compSize;
  nameLen_ = rd16(hdr_, 26);
  extraLen_ = rd16(hdr_, 28);
  if (nameLen_ == 0 || nameLen_ > kMaxNameLen) {
    fail("bad file name length");
    return;
  }
  if (extraLen_ > kMaxExtraLen) {
    fail("bad extra field length");
    return;
  }
}

void ZipReader::startEntry() {
  name_.assign(hdr_.data() + kFixedHeaderLen, nameLen_);
  const uint32_t declaredSize = dataRemaining_;
  crc_ = 0xFFFFFFFFu;
  hdr_.clear();
  phase_ = Phase::Data;
  visitor_.onEntryStart(name_, declaredSize);
  if (dataRemaining_ == 0) {
    visitor_.onEntryEnd((crc_ ^ 0xFFFFFFFFu) == expectedCrc_);
    headerNeed_ = 4;
    phase_ = Phase::HeaderSig;
  }
}

void ZipReader::consumeData(const uint8_t*& p, const uint8_t* end) {
  const std::size_t avail = static_cast<std::size_t>(end - p);
  const std::size_t take = std::min(static_cast<std::size_t>(dataRemaining_), avail);
  if (take) {
    crc_ = crc32Update(crc_, p, take);
    visitor_.onEntryData(p, take);
    p += take;
    dataRemaining_ -= static_cast<uint32_t>(take);
  }
  if (dataRemaining_ == 0) {
    visitor_.onEntryEnd((crc_ ^ 0xFFFFFFFFu) == expectedCrc_);
    headerNeed_ = 4;
    phase_ = Phase::HeaderSig;
  }
}

bool ZipReader::feed(const uint8_t* data, std::size_t n) {
  if (!ok_ || archiveEnded_) return false;
  const uint8_t* p = data;
  const uint8_t* end = data + n;
  while (p < end && ok_ && !archiveEnded_) {
    if (phase_ == Phase::Data) {
      consumeData(p, end);
      continue;
    }
    while (hdr_.size() < headerNeed_ && p < end) hdr_.push_back(static_cast<char>(*p++));
    if (hdr_.size() < headerNeed_) return true;

    if (phase_ == Phase::HeaderSig) {
      const uint32_t sig = rd32(hdr_, 0);
      if (sig == kLocalSig) {
        headerNeed_ = kFixedHeaderLen;
        phase_ = Phase::HeaderFixed;
      } else if (sig == kCentralSig || sig == kEocdSig) {
        // The first central-directory or EOCD record means the entries are done. Neither is
        // parsed; everything needed was already in the local headers.
        archiveEnded_ = true;
        phase_ = Phase::End;
        visitor_.onArchiveEnd();
      } else {
        return fail("not a zip");
      }
    } else if (phase_ == Phase::HeaderFixed) {
      parseFixedHeader();
      if (!ok_) return false;
      headerNeed_ = kFixedHeaderLen + nameLen_ + extraLen_;
      phase_ = Phase::HeaderVar;
    } else if (phase_ == Phase::HeaderVar) {
      startEntry();
    }
  }
  return ok_ && !archiveEnded_;
}

// Called once the upload is over. Ending exactly on a record boundary is accepted, since some
// writers omit the central directory; stopping anywhere else means the upload was cut short.
bool ZipReader::finish() {
  if (!ok_) return false;
  if (!archiveEnded_) {
    if (phase_ == Phase::HeaderSig && hdr_.empty()) {
      archiveEnded_ = true;
      phase_ = Phase::End;
      visitor_.onArchiveEnd();
    } else {
      return fail("truncated archive");
    }
  }
  return ok_;
}

}
}
