#include "platform/linux/ble/Att.h"

namespace awtrix::ble::att {

const char* errorText(uint8_t code) {
  switch (code) {
    case kInvalidHandle: return "invalid handle";
    case kReadNotPermitted: return "read not permitted";
    case kWriteNotPermitted: return "write not permitted";
    case kInvalidPdu: return "invalid request";
    case kInsufficientAuthentication: return "pairing required";
    case kRequestNotSupported: return "request not supported";
    case kInvalidOffset: return "invalid offset";
    case kAttributeNotFound: return "not found";
    case kAttributeNotLong: return "attribute not long";
    case kInvalidLength: return "invalid length";
    case kUnlikely: return "unlikely error";
    case kInsufficientEncryption: return "encryption required";
    case kUnsupportedGroupType: return "unsupported group type";
    default: return code >= 0x80 ? "application error" : "error";
  }
}

}
