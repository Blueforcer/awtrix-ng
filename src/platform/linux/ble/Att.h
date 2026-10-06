#pragma once

#include <cstdint>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble::att {

enum Op : uint8_t {
  kError = 0x01,
  kMtuReq = 0x02,
  kMtuRsp = 0x03,
  kFindInfoReq = 0x04,
  kFindInfoRsp = 0x05,
  kFindByTypeReq = 0x06,
  kFindByTypeRsp = 0x07,
  kReadByTypeReq = 0x08,
  kReadByTypeRsp = 0x09,
  kReadReq = 0x0a,
  kReadRsp = 0x0b,
  kReadBlobReq = 0x0c,
  kReadBlobRsp = 0x0d,
  kReadByGroupReq = 0x10,
  kReadByGroupRsp = 0x11,
  kWriteReq = 0x12,
  kWriteRsp = 0x13,
  kPrepareWriteReq = 0x16,
  kExecuteWriteReq = 0x18,
  kNotify = 0x1b,
  kIndicate = 0x1d,
  kConfirm = 0x1e,
  kWriteCmd = 0x52,
  kSignedWriteCmd = 0xd2,
};

enum Err : uint8_t {
  kInvalidHandle = 0x01,
  kReadNotPermitted = 0x02,
  kWriteNotPermitted = 0x03,
  kInvalidPdu = 0x04,
  kInsufficientAuthentication = 0x05,
  kRequestNotSupported = 0x06,
  kInvalidOffset = 0x07,
  kAttributeNotFound = 0x0a,
  kAttributeNotLong = 0x0b,
  kInvalidLength = 0x0d,
  kUnlikely = 0x0e,
  kInsufficientEncryption = 0x0f,
  kUnsupportedGroupType = 0x10,
};

enum Prop : uint8_t {
  kPropRead = 0x02,
  kPropWriteNoResponse = 0x04,
  kPropWrite = 0x08,
  kPropNotify = 0x10,
  kPropIndicate = 0x20,
};

constexpr uint16_t kPrimaryService = 0x2800;
constexpr uint16_t kCharacteristic = 0x2803;
constexpr uint16_t kClientConfig = 0x2902;
constexpr uint16_t kDefaultMtu = 23;
constexpr uint16_t kMaxMtu = 247;

// Requests and commands a peer sends us (server side) versus answers and notifications to
// what we asked (client side). One link carries both.
inline bool toServer(uint8_t op) {
  switch (op) {
    case kMtuReq: case kFindInfoReq: case kFindByTypeReq: case kReadByTypeReq: case kReadReq:
    case kReadBlobReq: case kReadByGroupReq: case kWriteReq: case kPrepareWriteReq:
    case kExecuteWriteReq: case kWriteCmd: case kSignedWriteCmd: case kConfirm:
      return true;
    default:
      return (op & 0x40) != 0;
  }
}

inline Bytes error(uint8_t reqOp, uint16_t handle, uint8_t code) {
  Bytes b{kError, reqOp};
  put16(b, handle);
  b.push_back(code);
  return b;
}

const char* errorText(uint8_t code);

}
