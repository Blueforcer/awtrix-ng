#include "core/render/TextEncoding.h"
#include "platform/linux/ble/Ancs.h"

namespace awtrix::ble::ancs {
namespace {

constexpr uint8_t kGetNotificationAttributes = 0;
constexpr uint8_t kAppIdentifier = 0, kTitle = 1, kMessage = 3;
constexpr std::size_t kHeader = 5;
constexpr int kAttributes = 3;


}

bool parseNotice(const Bytes& value, Notice& out) {
  if (value.size() != 8) return false;
  out.event = value[0];
  out.flags = value[1];
  out.category = value[2];
  out.count = value[3];
  out.uid = posix::le32(value.data() + 4);
  return true;
}

Bytes attributesRequest(uint32_t uid) {
  Bytes out{kGetNotificationAttributes};
  posix::put32(out, uid);
  out.push_back(kAppIdentifier);
  out.push_back(kTitle);
  put16(out, kTitleBytes);
  out.push_back(kMessage);
  put16(out, kMessageBytes);
  return out;
}

void Assembler::begin(uint32_t uid) {
  active_ = true;
  buffer_.clear();
  result_ = Attributes{};
  result_.uid = uid;
}

Assembler::Result Assembler::feed(const Bytes& value) {
  if (!active_) return Result::Waiting;
  buffer_.insert(buffer_.end(), value.begin(), value.end());
  if (buffer_.size() > kMaxBytes) {
    reset();
    return Result::Failed;
  }
  if (buffer_[0] != kGetNotificationAttributes || (buffer_.size() >= kHeader && posix::le32(&buffer_[1]) != result_.uid)) {
    buffer_.clear();
    return Result::Waiting;
  }
  std::size_t at = kHeader;
  int found = 0;
  Attributes next;
  next.uid = result_.uid;
  while (at + 3 <= buffer_.size()) {
    const uint8_t id = buffer_[at];
    const std::size_t length = le16(&buffer_[at + 1]);
    if (at + 3 + length > buffer_.size()) break;
    const std::string text(reinterpret_cast<const char*>(&buffer_[at + 3]), length);
    if (id == kAppIdentifier) next.app = text;
    else if (id == kTitle) next.title = text;
    else if (id == kMessage) next.message = text;
    at += 3 + length;
    ++found;
  }
  if (found < kAttributes) return Result::Waiting;
  trimUtf8(next.title);
  trimUtf8(next.message);
  result_ = std::move(next);
  reset();
  return Result::Done;
}

void trimUtf8(std::string& text) {
  text.resize(awtrix::text::clipBytes(text, text.size()).size());
}

}
