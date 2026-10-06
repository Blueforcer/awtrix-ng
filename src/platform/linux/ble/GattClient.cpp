#include "platform/linux/ble/GattClient.h"

#include <algorithm>

#include "platform/linux/ble/Att.h"

namespace awtrix::ble {

// Longest value a read follows with blob reads; the ATT limit for an attribute value.
constexpr std::size_t kMaxValue = 512;

void GattClient::start(int64_t nowMs) {
  now_ = nowMs;
  state_ = State::Discovering;
  queue(request(Kind::Mtu));
  queue(request(Kind::Services, 0, 1));
  pump(nowMs);
}

const GattClient::Characteristic* GattClient::find(const Uuid& service, const Uuid& characteristic) const {
  for (const Service& s : services_) {
    if (s.uuid != service) continue;
    for (const Characteristic& c : s.characteristics)
      if (c.uuid == characteristic) return &c;
  }
  return nullptr;
}

std::vector<const GattClient::Characteristic*> GattClient::findAll(const Uuid& service,
                                                                   const Uuid& characteristic) const {
  std::vector<const Characteristic*> out;
  for (const Service& s : services_) {
    if (s.uuid != service) continue;
    for (const Characteristic& c : s.characteristics)
      if (c.uuid == characteristic) out.push_back(&c);
  }
  return out;
}

void GattClient::read(uint16_t handle, Done done) {
  if (state_ == State::Broken) {
    done(false, 0, {});
    return;
  }
  Op op = request(Kind::Read, handle);
  op.done = std::move(done);
  queue(std::move(op));
  pump(now_);
}

void GattClient::write(uint16_t handle, Bytes value, bool withResponse, Done done) {
  if (state_ == State::Broken || value.size() + 3 > mtu_) {
    done(false, state_ == State::Broken ? 0 : att::kInvalidLength, {});
    return;
  }
  if (!withResponse) {
    Bytes pdu{att::kWriteCmd};
    put16(pdu, handle);
    pdu.insert(pdu.end(), value.begin(), value.end());
    const bool sent = send_(pdu);
    done(sent, 0, {});
    return;
  }
  Op op = request(Kind::Write, handle);
  op.value = std::move(value);
  op.done = std::move(done);
  queue(std::move(op));
  pump(now_);
}

void GattClient::queue(Op op) { ops_.push_back(std::move(op)); }

void GattClient::pump(int64_t nowMs) {
  while (!waiting_ && !ops_.empty() && state_ != State::Broken) {
    if (!issue(ops_.front())) {
      fail("send failed");
      return;
    }
    waiting_ = true;
    deadline_ = nowMs + kRequestTimeoutMs;
  }
}

bool GattClient::issue(Op& op) {
  Bytes pdu;
  switch (op.kind) {
    case Kind::Mtu:
      pdu = {att::kMtuReq};
      put16(pdu, att::kMaxMtu);
      break;
    case Kind::Services:
      pdu = {att::kReadByGroupReq};
      put16(pdu, op.cursor);
      put16(pdu, 0xffff);
      put16(pdu, att::kPrimaryService);
      break;
    case Kind::Characteristics:
      pdu = {att::kReadByTypeReq};
      put16(pdu, op.cursor);
      put16(pdu, services_[op.index].end);
      put16(pdu, att::kCharacteristic);
      break;
    case Kind::Descriptors:
      pdu = {att::kFindInfoReq};
      put16(pdu, op.cursor);
      put16(pdu, op.handle);
      break;
    case Kind::Read:
      pdu = {op.value.empty() ? att::kReadReq : att::kReadBlobReq};
      put16(pdu, op.handle);
      if (!op.value.empty()) put16(pdu, static_cast<uint16_t>(op.value.size()));
      break;
    case Kind::Write:
      pdu = {att::kWriteReq};
      put16(pdu, op.handle);
      pdu.insert(pdu.end(), op.value.begin(), op.value.end());
      break;
  }
  return send_(pdu);
}

void GattClient::finish(bool ok, uint8_t attError, const Bytes& value) {
  Op op = std::move(ops_.front());
  ops_.pop_front();
  waiting_ = false;
  if (op.done) op.done(ok, attError, value);
}

void GattClient::fail(const std::string& why) {
  if (state_ == State::Broken) return;
  const bool discovering = state_ == State::Discovering;
  state_ = State::Broken;
  error_ = why;
  waiting_ = false;
  std::deque<Op> pending;
  pending.swap(ops_);
  for (Op& op : pending)
    if (op.done) op.done(false, 0, {});
  if (discovering && onReady) onReady(false);
}

void GattClient::tick(int64_t nowMs) {
  now_ = nowMs;
  if (waiting_ && nowMs >= deadline_) fail("the peer did not answer");
}

void GattClient::receive(const uint8_t* p, std::size_t n, int64_t nowMs) {
  now_ = nowMs;
  if (n == 0) return;
  if (p[0] == att::kNotify || p[0] == att::kIndicate) {
    if (n < 3) return;
    if (p[0] == att::kIndicate) send_(Bytes{att::kConfirm});
    if (onValue) onValue(le16(p + 1), Bytes(p + 3, p + n));
    return;
  }
  if (!waiting_ || ops_.empty()) return;
  answer(p, n);
  pump(nowMs);
}

void GattClient::answer(const uint8_t* p, std::size_t n) {
  Op& op = ops_.front();
  const bool isError = p[0] == att::kError && n >= 5;
  const uint8_t code = isError ? p[4] : 0;
  switch (op.kind) {
    case Kind::Mtu:
      if (!isError && p[0] == att::kMtuRsp && n >= 3)
        mtu_ = std::max<uint16_t>(att::kDefaultMtu, std::min<uint16_t>(att::kMaxMtu, le16(p + 1)));
      finish(true, 0, {});
      return;
    case Kind::Services: {
      if (isError) {
        if (code != att::kAttributeNotFound) return fail(std::string("service discovery: ") + att::errorText(code));
        finish(true, 0, {});
        discoveryStep();
        return;
      }
      if (p[0] != att::kReadByGroupRsp || n < 2 || p[1] < 6) return fail("service discovery: bad answer");
      const std::size_t len = p[1];
      uint16_t last = 0;
      for (std::size_t i = 2; i + len <= n; i += len) {
        Service s;
        s.start = le16(p + i);
        s.end = le16(p + i + 2);
        if (!Uuid::fromWire(p + i + 4, len - 4, s.uuid) || s.end < s.start) return fail("service discovery: bad entry");
        last = s.end;
        services_.push_back(std::move(s));
      }
      if (last == 0xffff || last < op.cursor) {
        finish(true, 0, {});
        discoveryStep();
      } else {
        op.cursor = static_cast<uint16_t>(last + 1);
        waiting_ = false;
      }
      return;
    }
    case Kind::Characteristics: {
      Service& s = services_[op.index];
      bool more = false;
      if (isError) {
        if (code != att::kAttributeNotFound) return fail(std::string("characteristic discovery: ") + att::errorText(code));
      } else {
        if (p[0] != att::kReadByTypeRsp || n < 2 || p[1] < 7) return fail("characteristic discovery: bad answer");
        const std::size_t len = p[1];
        uint16_t last = 0;
        for (std::size_t i = 2; i + len <= n; i += len) {
          Characteristic c;
          c.declaration = le16(p + i);
          c.props = p[i + 2];
          c.value = le16(p + i + 3);
          if (!Uuid::fromWire(p + i + 5, len - 5, c.uuid)) return fail("characteristic discovery: bad entry");
          last = c.declaration;
          s.characteristics.push_back(std::move(c));
        }
        more = last >= op.cursor && last < s.end;
        if (more) op.cursor = static_cast<uint16_t>(last + 1);
      }
      if (more) {
        waiting_ = false;
        return;
      }
      for (std::size_t k = 0; k < s.characteristics.size(); ++k)
        s.characteristics[k].end = k + 1 < s.characteristics.size()
                                       ? static_cast<uint16_t>(s.characteristics[k + 1].declaration - 1)
                                       : s.end;
      finish(true, 0, {});
      discoveryStep();
      return;
    }
    case Kind::Descriptors: {
      Characteristic* c = nullptr;
      std::size_t flat = 0;
      for (Service& s : services_)
        for (Characteristic& ch : s.characteristics)
          if (flat++ == op.index) c = &ch;
      bool done = true;
      if (!isError && c && p[0] == att::kFindInfoRsp && n >= 2) {
        const std::size_t width = p[1] == 1 ? 2 : 16;
        uint16_t last = 0;
        for (std::size_t i = 2; i + 2 + width <= n; i += 2 + width) {
          last = le16(p + i);
          Uuid type;
          if (!Uuid::fromWire(p + i + 2, width, type)) continue;
          c->descriptors.push_back({type, last});
          if (type == Uuid::from16(att::kClientConfig)) c->clientConfig = last;
        }
        if (last >= op.cursor && last < op.handle) {
          op.cursor = static_cast<uint16_t>(last + 1);
          done = false;
        }
      } else if (isError && code != att::kAttributeNotFound) {
        return fail(std::string("descriptor discovery: ") + att::errorText(code));
      }
      if (!done) {
        waiting_ = false;
        return;
      }
      finish(true, 0, {});
      discoveryStep();
      return;
    }
    case Kind::Read: {
      if (isError) {
        if (!op.value.empty() && code == att::kAttributeNotLong) return finish(true, 0, op.value);
        return finish(false, code, {});
      }
      if ((p[0] != att::kReadRsp && p[0] != att::kReadBlobRsp)) return finish(false, att::kUnlikely, {});
      const std::size_t chunk = n - 1;
      op.value.insert(op.value.end(), p + 1, p + n);
      if (chunk + 1 == mtu_ && op.value.size() < kMaxValue && !op.value.empty()) {
        waiting_ = false;
        return;
      }
      Bytes value = std::move(op.value);
      return finish(true, 0, value);
    }
    case Kind::Write:
      if (isError) return finish(false, code, {});
      return finish(p[0] == att::kWriteRsp, p[0] == att::kWriteRsp ? 0 : att::kUnlikely, {});
  }
}

void GattClient::discoveryStep() {
  if (state_ != State::Discovering) return;
  // Characteristics per service in order, then the client configuration descriptor of
  // everything that notifies or indicates. Each call queues the next request or finishes.
  while (charCursor_ < services_.size()) {
    const std::size_t i = charCursor_++;
    const Service& s = services_[i];
    if (s.end > s.start) {
      queue(request(Kind::Characteristics, 0, static_cast<uint16_t>(s.start + 1), i));
      return;
    }
  }
  std::size_t flat = 0;
  for (Service& s : services_)
    for (Characteristic& c : s.characteristics) {
      const std::size_t here = flat++;
      if (here < descCursor_) continue;
      descCursor_ = here + 1;
      if ((c.props & (att::kPropNotify | att::kPropIndicate)) && c.end > c.value) {
        queue(request(Kind::Descriptors, c.end, static_cast<uint16_t>(c.value + 1), here));
        return;
      }
    }
  state_ = State::Ready;
  if (onReady) onReady(true);
}
}
