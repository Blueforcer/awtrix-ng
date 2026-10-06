#include "platform/linux/ble/GattServer.h"

#include <algorithm>

#include "platform/linux/ble/Att.h"

namespace awtrix::ble {
namespace {

constexpr uint16_t kGapService = 0x1800;
constexpr uint16_t kGattService = 0x1801;
constexpr uint16_t kDeviceName = 0x2a00;
constexpr uint16_t kAppearance = 0x2a01;
constexpr uint16_t kServiceChanged = 0x2a05;
constexpr uint16_t kServiceChangedValue = 8;
constexpr uint16_t kFirstScriptHandle = 10;
constexpr std::size_t kMaxValue = 512;

Bytes declaration(uint8_t props, uint16_t valueHandle, const Uuid& uuid) {
  Bytes v{props};
  put16(v, valueHandle);
  uuid.appendWire(v);
  return v;
}

Bytes uuidValue(uint16_t v) {
  Bytes b;
  put16(b, v);
  return b;
}

}

GattServer::GattServer() {
  auto add = [this](uint16_t handle, uint16_t type, Kind kind, Bytes value, uint8_t props = 0, uint16_t configures = 0) {
    Attribute a;
    a.handle = handle;
    a.type = Uuid::from16(type);
    a.kind = kind;
    a.value = std::move(value);
    a.props = props;
    a.configures = configures;
    attributes_.push_back(std::move(a));
  };
  add(1, att::kPrimaryService, Kind::Service, uuidValue(kGapService));
  add(2, att::kCharacteristic, Kind::Declaration, declaration(att::kPropRead, 3, Uuid::from16(kDeviceName)));
  add(3, kDeviceName, Kind::Value, {}, att::kPropRead);
  add(4, att::kCharacteristic, Kind::Declaration, declaration(att::kPropRead, 5, Uuid::from16(kAppearance)));
  add(5, kAppearance, Kind::Value, {0, 0}, att::kPropRead);
  add(6, att::kPrimaryService, Kind::Service, uuidValue(kGattService));
  add(7, att::kCharacteristic, Kind::Declaration,
      declaration(att::kPropIndicate, kServiceChangedValue, Uuid::from16(kServiceChanged)));
  add(kServiceChangedValue, kServiceChanged, Kind::Value, {0, 0, 0, 0}, att::kPropIndicate);
  add(9, att::kClientConfig, Kind::ClientConfig, {}, 0, kServiceChangedValue);
  nextHandle_ = kFirstScriptHandle;
}

void GattServer::setDeviceName(const std::string& name) {
  if (Attribute* a = find(3)) a->value.assign(name.begin(), name.end());
}

GattServer::Attribute* GattServer::find(uint16_t handle) {
  for (Attribute& a : attributes_)
    if (a.handle == handle) return &a;
  return nullptr;
}

int GattServer::addService(const Uuid& uuid, const std::vector<CharacteristicSpec>& characteristics,
                           std::vector<uint16_t>& valueHandles) {
  std::size_t needed = 1;
  for (const CharacteristicSpec& c : characteristics)
    needed += 2 + ((c.props & (att::kPropNotify | att::kPropIndicate)) ? 1 : 0) + c.descriptors.size();
  if (nextHandle_ + needed > 0xffff) return 0;
  const int id = nextService_++;
  const uint16_t first = nextHandle_;
  Attribute service;
  service.handle = nextHandle_++;
  service.type = Uuid::from16(att::kPrimaryService);
  service.kind = Kind::Service;
  uuid.appendWire(service.value);
  service.service = id;
  attributes_.push_back(std::move(service));
  valueHandles.clear();
  for (const CharacteristicSpec& c : characteristics) {
    Attribute decl;
    decl.handle = nextHandle_++;
    decl.type = Uuid::from16(att::kCharacteristic);
    decl.kind = Kind::Declaration;
    decl.value = declaration(c.props, nextHandle_, c.uuid);
    decl.service = id;
    attributes_.push_back(std::move(decl));
    Attribute value;
    value.handle = nextHandle_++;
    value.type = c.uuid;
    value.kind = Kind::Value;
    value.value = c.value;
    value.props = c.props;
    value.encrypted = c.encrypted;
    value.service = id;
    valueHandles.push_back(value.handle);
    const uint16_t valueHandle = value.handle;
    attributes_.push_back(std::move(value));
    if (c.props & (att::kPropNotify | att::kPropIndicate)) {
      Attribute config;
      config.handle = nextHandle_++;
      config.type = Uuid::from16(att::kClientConfig);
      config.kind = Kind::ClientConfig;
      config.encrypted = c.encrypted;
      config.configures = valueHandle;
      config.service = id;
      attributes_.push_back(std::move(config));
    }
    for (const auto& [type, content] : c.descriptors) {
      Attribute descriptor;
      descriptor.handle = nextHandle_++;
      descriptor.type = type;
      descriptor.kind = Kind::Descriptor;
      descriptor.value = content;
      descriptor.encrypted = c.encrypted;
      descriptor.service = id;
      attributes_.push_back(std::move(descriptor));
    }
  }
  announce(first, static_cast<uint16_t>(nextHandle_ - 1));
  return id;
}

void GattServer::removeService(int id) {
  uint16_t lo = 0xffff, hi = 0;
  for (const Attribute& a : attributes_)
    if (a.service == id) lo = std::min(lo, a.handle), hi = std::max(hi, a.handle);
  if (hi == 0) return;
  attributes_.erase(std::remove_if(attributes_.begin(), attributes_.end(),
                                   [id](const Attribute& a) { return a.service == id; }),
                    attributes_.end());
  for (auto& [link, state] : links_)
    for (auto it = state.config.begin(); it != state.config.end();)
      it = it->first >= lo && it->first <= hi ? state.config.erase(it) : std::next(it);
  announce(lo, hi);
}

bool GattServer::hasService(const Uuid& uuid) const {
  for (const Attribute& a : attributes_) {
    if (a.kind != Kind::Service) continue;
    Uuid u;
    if (Uuid::fromWire(a.value.data(), a.value.size(), u) && u == uuid) return true;
  }
  return false;
}

bool GattServer::setValue(uint16_t valueHandle, Bytes value) {
  Attribute* a = find(valueHandle);
  if (!a || a->kind != Kind::Value || a->handle < kFirstScriptHandle || value.size() > kMaxValue) return false;
  a->value = std::move(value);
  return true;
}

void GattServer::linkUp(int link) { links_[link] = Link{}; }
void GattServer::linkDown(int link) { links_.erase(link); }

uint16_t GattServer::mtu(int link) const {
  auto it = links_.find(link);
  return it == links_.end() ? att::kDefaultMtu : it->second.mtu;
}

uint16_t GattServer::groupEnd(std::size_t index) const {
  for (std::size_t i = index + 1; i < attributes_.size(); ++i)
    if (attributes_[i].kind == Kind::Service) return static_cast<uint16_t>(attributes_[i].handle - 1);
  return attributes_.back().handle;
}

bool GattServer::readable(const Attribute& a, int securityLevel, uint8_t& error) const {
  if (a.kind == Kind::Value && !(a.props & att::kPropRead)) {
    error = att::kReadNotPermitted;
    return false;
  }
  if (a.encrypted && securityLevel < 2) {
    error = att::kInsufficientAuthentication;
    return false;
  }
  return true;
}

Bytes GattServer::readValue(int link, const Attribute& a) const {
  if (a.kind != Kind::ClientConfig) return a.value;
  auto l = links_.find(link);
  uint16_t v = 0;
  if (l != links_.end()) {
    auto c = l->second.config.find(a.configures);
    if (c != l->second.config.end()) v = c->second;
  }
  Bytes out;
  put16(out, v);
  return out;
}

Bytes GattServer::handle(int link, const uint8_t* p, std::size_t n, int securityLevel) {
  if (n == 0) return {};
  const uint8_t op = p[0];
  Link& state = links_[link];
  const std::size_t mtu = state.mtu;
  auto range = [&](uint16_t& start, uint16_t& end) -> Bytes {
    start = le16(p + 1);
    end = le16(p + 3);
    if (start == 0 || start > end) return att::error(op, start, att::kInvalidHandle);
    return {};
  };
  switch (op) {
    case att::kMtuReq: {
      if (n < 3) return att::error(op, 0, att::kInvalidPdu);
      state.mtu = std::max<uint16_t>(att::kDefaultMtu, std::min<uint16_t>(le16(p + 1), att::kMaxMtu));
      Bytes rsp{att::kMtuRsp};
      put16(rsp, att::kMaxMtu);
      return rsp;
    }
    case att::kFindInfoReq: {
      if (n < 5) return att::error(op, 0, att::kInvalidPdu);
      uint16_t start, end;
      if (Bytes e = range(start, end); !e.empty()) return e;
      Bytes rsp{att::kFindInfoRsp, 0};
      for (const Attribute& a : attributes_) {
        if (a.handle < start || a.handle > end) continue;
        const uint8_t format = a.type.isShort() ? 1 : 2;
        if (rsp[1] == 0) rsp[1] = format;
        if (format != rsp[1] || rsp.size() + 2 + a.type.wireSize() > mtu) break;
        put16(rsp, a.handle);
        a.type.appendWire(rsp);
      }
      return rsp[1] ? rsp : att::error(op, start, att::kAttributeNotFound);
    }
    case att::kFindByTypeReq: {
      if (n < 7) return att::error(op, 0, att::kInvalidPdu);
      uint16_t start, end;
      if (Bytes e = range(start, end); !e.empty()) return e;
      const Uuid type = Uuid::from16(le16(p + 5));
      const Bytes wanted(p + 7, p + n);
      Bytes rsp{att::kFindByTypeRsp};
      for (std::size_t i = 0; i < attributes_.size(); ++i) {
        const Attribute& a = attributes_[i];
        if (a.handle < start || a.handle > end || a.type != type || a.value != wanted) continue;
        if (rsp.size() + 4 > mtu) break;
        put16(rsp, a.handle);
        put16(rsp, a.kind == Kind::Service ? groupEnd(i) : a.handle);
      }
      return rsp.size() > 1 ? rsp : att::error(op, start, att::kAttributeNotFound);
    }
    case att::kReadByTypeReq:
    case att::kReadByGroupReq: {
      if (n != 7 && n != 21) return att::error(op, 0, att::kInvalidPdu);
      uint16_t start, end;
      if (Bytes e = range(start, end); !e.empty()) return e;
      Uuid type;
      Uuid::fromWire(p + 5, n - 5, type);
      const bool group = op == att::kReadByGroupReq;
      if (group && type != Uuid::from16(att::kPrimaryService)) return att::error(op, start, att::kUnsupportedGroupType);
      Bytes rsp{static_cast<uint8_t>(op + 1), 0};
      for (std::size_t i = 0; i < attributes_.size(); ++i) {
        const Attribute& a = attributes_[i];
        if (a.handle < start || a.handle > end || a.type != type) continue;
        uint8_t error = 0;
        if (!readable(a, securityLevel, error)) {
          if (rsp[1] == 0) return att::error(op, a.handle, error);
          break;
        }
        Bytes value = readValue(link, a);
        const std::size_t head = group ? 4 : 2;
        const std::size_t room = std::min<std::size_t>(mtu - 2, 255) - head;
        if (value.size() > room) value.resize(room);
        const std::size_t len = head + value.size();
        if (rsp[1] == 0) rsp[1] = static_cast<uint8_t>(len);
        if (len != rsp[1] || rsp.size() + len > mtu) break;
        put16(rsp, a.handle);
        if (group) put16(rsp, groupEnd(i));
        rsp.insert(rsp.end(), value.begin(), value.end());
      }
      return rsp[1] ? rsp : att::error(op, start, att::kAttributeNotFound);
    }
    case att::kReadReq:
    case att::kReadBlobReq: {
      if (n < (op == att::kReadReq ? 3u : 5u)) return att::error(op, 0, att::kInvalidPdu);
      const uint16_t h = le16(p + 1);
      const Attribute* a = find(h);
      if (!a) return att::error(op, h, att::kInvalidHandle);
      uint8_t error = 0;
      if (!readable(*a, securityLevel, error)) return att::error(op, h, error);
      const Bytes value = readValue(link, *a);
      const std::size_t offset = op == att::kReadBlobReq ? le16(p + 3) : 0;
      if (offset > value.size()) return att::error(op, h, att::kInvalidOffset);
      Bytes rsp{static_cast<uint8_t>(op + 1)};
      const std::size_t take = std::min(value.size() - offset, mtu - 1);
      rsp.insert(rsp.end(), value.begin() + static_cast<long>(offset), value.begin() + static_cast<long>(offset + take));
      return rsp;
    }
    case att::kWriteReq:
    case att::kWriteCmd: {
      const bool command = op == att::kWriteCmd;
      if (n < 3) return command ? Bytes{} : att::error(op, 0, att::kInvalidPdu);
      const uint16_t h = le16(p + 1);
      Attribute* a = find(h);
      uint8_t error = 0;
      if (!a) error = att::kInvalidHandle;
      else if (a->encrypted && securityLevel < 2) error = att::kInsufficientAuthentication;
      else if (a->kind == Kind::ClientConfig) {
        if (n != 5) error = att::kInvalidLength;
      } else if (a->kind != Kind::Value || !(a->props & (att::kPropWrite | att::kPropWriteNoResponse))) {
        error = att::kWriteNotPermitted;
      } else if (n - 3 > kMaxValue) {
        error = att::kInvalidLength;
      }
      if (error) return command ? Bytes{} : att::error(op, h, error);
      if (a->kind == Kind::ClientConfig) {
        const uint16_t config = le16(p + 3);
        state.config[a->configures] = config;
        if (onSubscribe && a->configures >= kFirstScriptHandle) onSubscribe(link, a->configures, config);
      } else {
        a->value.assign(p + 3, p + n);
        if (onWrite) onWrite(link, h, a->value);
      }
      return command ? Bytes{} : Bytes{att::kWriteRsp};
    }
    case att::kConfirm:
      state.awaitingConfirm = false;
      return {};
    case att::kSignedWriteCmd:
      return {};
    default:
      if (op & 0x40) return {};
      return att::error(op, 0, att::kRequestNotSupported);
  }
}

void GattServer::publish(uint16_t valueHandle) {
  const Attribute* a = find(valueHandle);
  if (!a || a->kind != Kind::Value) return;
  for (auto& [link, state] : links_) {
    auto c = state.config.find(valueHandle);
    if (c == state.config.end() || c->second == 0) continue;
    const bool indicate = (c->second & 2) && (a->props & att::kPropIndicate);
    Bytes pdu{indicate ? att::kIndicate : att::kNotify};
    put16(pdu, valueHandle);
    const std::size_t take = std::min<std::size_t>(a->value.size(), state.mtu - 3);
    pdu.insert(pdu.end(), a->value.begin(), a->value.begin() + static_cast<long>(take));
    if (indicate) {
      // A value that changes faster than the peer confirms keeps only its newest update.
      for (auto it = state.indications.begin(); it != state.indications.end();)
        it = le16(it->data() + 1) == valueHandle ? state.indications.erase(it) : std::next(it);
      state.indications.push_back(std::move(pdu));
    } else {
      state.notifications.push_back(std::move(pdu));
    }
  }
}

std::vector<std::pair<int, Bytes>> GattServer::drain(const std::function<int(int)>& security) {
  std::vector<std::pair<int, Bytes>> out;
  for (auto& [link, state] : links_) {
    const int level = security ? security(link) : 1;
    const auto permitted = [&](const Bytes& pdu) {
      const Attribute* a = find(le16(pdu.data() + 1));
      return a && (!a->encrypted || level >= 2);
    };
    while (!state.notifications.empty()) {
      if (permitted(state.notifications.front()))
        out.emplace_back(link, std::move(state.notifications.front()));
      state.notifications.pop_front();
    }
    while (!state.awaitingConfirm && !state.indications.empty()) {
      if (permitted(state.indications.front())) {
        out.emplace_back(link, std::move(state.indications.front()));
        state.awaitingConfirm = true;
      }
      state.indications.pop_front();
    }
  }
  return out;
}

void GattServer::announce(uint16_t start, uint16_t end) {
  Attribute* changed = find(kServiceChangedValue);
  if (!changed) return;
  changed->value.clear();
  put16(changed->value, start);
  put16(changed->value, end);
  publish(kServiceChangedValue);
}

}
