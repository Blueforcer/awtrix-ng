#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace awtrix {
namespace backup {

class ZipVisitor {
 public:
  virtual ~ZipVisitor() = default;
  virtual void onEntryStart(const std::string& name, uint32_t size) = 0;
  virtual void onEntryData(const uint8_t* data, std::size_t n) = 0;
  virtual void onEntryEnd(bool crcOk) = 0;
  virtual void onArchiveEnd() = 0;
};

// Streaming ZIP reader for restore uploads. Bytes arrive in HTTP-sized chunks and nothing is
// seekable, so it walks local file headers forwards and stops at the central directory.
class ZipReader {
 public:
  explicit ZipReader(ZipVisitor& visitor);

  // Returns false once there is nothing more to feed, which covers both an error and a cleanly
  // ended archive. Use ok() to tell those apart.
  bool feed(const uint8_t* data, std::size_t n);

  bool finish();

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

 private:
  enum class Phase { HeaderSig, HeaderFixed, HeaderVar, Data, End };

  bool fail(const char* message);
  void parseFixedHeader();
  void startEntry();
  void consumeData(const uint8_t*& p, const uint8_t* end);

  ZipVisitor& visitor_;
  Phase phase_ = Phase::HeaderSig;
  bool ok_ = true;
  bool archiveEnded_ = false;
  std::string error_;

  std::string hdr_;
  std::size_t headerNeed_ = 4;
  uint16_t nameLen_ = 0;
  uint16_t extraLen_ = 0;

  std::string name_;
  uint32_t expectedCrc_ = 0;
  uint32_t dataRemaining_ = 0;
  uint32_t crc_ = 0;
};

}
}
