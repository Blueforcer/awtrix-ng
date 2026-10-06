#pragma once

#include <algorithm>
#include <sys/socket.h>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble::linux_detail {

constexpr int kAfBluetooth = 31;
constexpr int kProtoL2cap = 0;
constexpr int kProtoHci = 1;
constexpr unsigned short kChannelRaw = 0;
constexpr unsigned short kChannelControl = 3;
constexpr unsigned short kNoDevice = 0xffff;
constexpr int kSolHci = 0;
constexpr int kHciFilter = 2;
constexpr int kSolBluetooth = 274;
constexpr int kBtSecurity = 4;
constexpr int kSolL2cap = 6;
constexpr int kL2capConninfo = 2;
constexpr uint8_t kLePublic = 1;
constexpr uint8_t kLeRandom = 2;
constexpr unsigned short kAttCid = 4;

struct SockaddrHci {
  sa_family_t family;
  unsigned short dev;
  unsigned short channel;
};
struct HciFilter {
  uint32_t typeMask;
  uint32_t eventMask[2];
  uint16_t opcode;
};
struct SockaddrL2 {
  sa_family_t family;
  unsigned short psm;
  uint8_t bdaddr[6];
  unsigned short cid;
  uint8_t bdaddrType;
};
struct BtSecurity {
  uint8_t level;
  uint8_t keySize;
};
struct L2capConninfo {
  uint16_t handle;
  uint8_t deviceClass[3];
};

enum MgmtOp : uint16_t {
  kReadIndexList = 0x0003,
  kReadInfo = 0x0004,
  kSetPowered = 0x0005,
  kSetConnectable = 0x0007,
  kSetBondable = 0x0009,
  kSetLocalName = 0x000f,
  kLoadLtks = 0x0013,
  kSetIoCapability = 0x0018,
  kUnpairDevice = 0x001b,
  kUserConfirmReply = 0x001c,
  kUserPasskeyNegativeReply = 0x001f,
  kLoadIrks = 0x0030,
  kReadAdvFeatures = 0x003d,
  kAddAdvertising = 0x003e,
  kRemoveAdvertising = 0x003f,
};

enum MgmtEvent : uint16_t {
  kCommandComplete = 0x0001,
  kCommandStatus = 0x0002,
  kIndexAdded = 0x0004,
  kIndexRemoved = 0x0005,
  kNewLtk = 0x000a,
  kDeviceDisconnected = 0x000c,
  kUserConfirmRequest = 0x000f,
  kUserPasskeyRequest = 0x0010,
  kAuthFailed = 0x0011,
  kNewIrk = 0x0018,
};

constexpr std::size_t kMaxQueued = 64;
constexpr int64_t kScanQuietMs = 8000;
constexpr int64_t kDirectConnectMs = 10000;
constexpr int64_t kCancelMs = 2000;
// The kernel's own advertising interval (hci_alloc_dev), in 0.625 ms slots: 1.28 s.
constexpr uint16_t kAdvertisingInterval = 0x0800;

inline Address fromWire(const uint8_t* b, uint8_t type) {
  Address a;
  std::copy(b, b + 6, a.b.begin());
  a.random = type == kLeRandom;
  return a;
}

}  // namespace awtrix::ble::linux_detail
