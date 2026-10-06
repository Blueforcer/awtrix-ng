#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// The client half of one link: learns the peer's services once, then runs reads, writes and
// subscriptions one request at a time, as ATT allows only one outstanding request per bearer.
class GattClient {
 public:
  struct Descriptor {
    Uuid uuid;
    uint16_t handle = 0;
  };
  struct Characteristic {
    Uuid uuid;
    uint16_t declaration = 0;
    uint16_t value = 0;
    uint16_t end = 0;
    uint16_t clientConfig = 0;
    uint8_t props = 0;
    // Every descriptor of a characteristic that can notify, the configuration one included.
    std::vector<Descriptor> descriptors;
    uint16_t descriptor(const Uuid& uuid) const {
      for (const Descriptor& d : descriptors)
        if (d.uuid == uuid) return d.handle;
      return 0;
    }
  };
  struct Service {
    Uuid uuid;
    uint16_t start = 0;
    uint16_t end = 0;
    std::vector<Characteristic> characteristics;
  };
  // attError is the peer's ATT error code, 0 for a local failure such as a timeout.
  using Done = std::function<void(bool ok, uint8_t attError, const Bytes& value)>;
  using Send = std::function<bool(const Bytes& pdu)>;

  static constexpr int64_t kRequestTimeoutMs = 10000;

  explicit GattClient(Send send) : send_(std::move(send)) {}

  void start(int64_t nowMs);
  bool ready() const { return state_ == State::Ready; }
  bool broken() const { return state_ == State::Broken; }
  const std::string& error() const { return error_; }
  uint16_t mtu() const { return mtu_; }
  const std::vector<Service>& services() const { return services_; }
  const Characteristic* find(const Uuid& service, const Uuid& characteristic) const;
  // Every instance: a peer may carry the same service twice, as a watch does when two apps on it
  // each register a heart-rate service.
  std::vector<const Characteristic*> findAll(const Uuid& service, const Uuid& characteristic) const;

  void read(uint16_t handle, Done done);
  void write(uint16_t handle, Bytes value, bool withResponse, Done done);

  // Everything the peer sent that is an answer, a notification or an indication.
  void receive(const uint8_t* pdu, std::size_t n, int64_t nowMs);
  void tick(int64_t nowMs);
  // The link is gone: every queued operation is answered as failed, and nothing is sent again.
  void abort(const std::string& why) { fail(why); }

  std::function<void(bool ok)> onReady;
  std::function<void(uint16_t handle, const Bytes& value)> onValue;

 private:
  enum class State { Idle, Discovering, Ready, Broken };
  enum class Kind { Mtu, Services, Characteristics, Descriptors, Read, Write };
  struct Op {
    Kind kind = Kind::Mtu;
    uint16_t handle = 0;
    uint16_t cursor = 0;
    std::size_t index = 0;
    Bytes value;
    Done done;
  };

  static Op request(Kind kind, uint16_t handle = 0, uint16_t cursor = 0, std::size_t index = 0) {
    Op op;
    op.kind = kind;
    op.handle = handle;
    op.cursor = cursor;
    op.index = index;
    return op;
  }
  void queue(Op op);
  void pump(int64_t nowMs);
  bool issue(Op& op);
  void finish(bool ok, uint8_t attError, const Bytes& value);
  void fail(const std::string& why);
  void discoveryStep();
  void answer(const uint8_t* p, std::size_t n);

  Send send_;
  State state_ = State::Idle;
  std::string error_;
  uint16_t mtu_ = 23;
  std::vector<Service> services_;
  std::deque<Op> ops_;
  std::size_t charCursor_ = 0;
  std::size_t descCursor_ = 0;
  bool waiting_ = false;
  int64_t deadline_ = 0;
  int64_t now_ = 0;
};

}
