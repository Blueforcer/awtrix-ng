#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace awtrix::tc002::wake {

class Flat {
 public:
  struct Table {
    std::size_t pos = 0, vtable = 0, vsize = 0, tsize = 0;
    bool present = false;
  };
  struct Vector {
    std::size_t pos = 0, count = 0;
  };

  Flat(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}
  bool ok() const { return ok_; }

  Table root() {
    uint32_t offset = 0;
    if (!read(0, offset)) return {};
    return tableAt(offset);
  }
  template <typename T>
  T field(const Table& t, int id, T fallback) {
    T value = fallback;
    if (const std::size_t at = slot(t, id, sizeof(T))) read(at, value);
    return ok_ ? value : fallback;
  }
  Table table(const Table& t, int id) {
    const std::size_t at = slot(t, id, 4);
    return at ? tableAt(target(at)) : Table{};
  }
  Vector vector(const Table& t, int id, std::size_t width) {
    const std::size_t at = slot(t, id, 4);
    return at ? vectorAt(target(at), width) : Vector{};
  }
  std::string_view string(const Table& t, int id) {
    const Vector v = vector(t, id, 1);
    return ok_ ? std::string_view(reinterpret_cast<const char*>(data_ + v.pos), v.count) : std::string_view();
  }
  Table tableIn(const Vector& v, std::size_t i) {
    return i < v.count ? tableAt(target(v.pos + 4 * i)) : Table{};
  }
  template <typename T>
  T element(const Vector& v, std::size_t i) {
    T value{};
    if (i < v.count) read(v.pos + i * sizeof(T), value);
    return value;
  }
  const uint8_t* at(std::size_t pos) const { return data_ + pos; }
  bool contains(uint64_t pos, uint64_t length) const { return pos <= size_ && length <= size_ - pos; }

 private:
  template <typename T>
  bool read(std::size_t pos, T& out) {
    if (!ok_ || !contains(pos, sizeof(T))) return ok_ = false;
    std::memcpy(&out, data_ + pos, sizeof(T));
    return true;
  }
  std::size_t target(std::size_t at) {
    uint32_t offset = 0;
    if (!read(at, offset)) return 0;
    if (offset == 0 || !contains(at, offset)) {
      ok_ = false;
      return 0;
    }
    return at + offset;
  }
  Table tableAt(std::size_t pos) {
    int32_t back = 0;
    if (!ok_ || !read(pos, back)) return {};
    const int64_t vtable = static_cast<int64_t>(pos) - back;
    uint16_t vsize = 0, tsize = 0;
    if (vtable < 0 || !read(static_cast<std::size_t>(vtable), vsize) ||
        !read(static_cast<std::size_t>(vtable) + 2, tsize))
      return {};
    if (vsize < 4 || (vsize & 1) || tsize < 4 || !contains(static_cast<std::size_t>(vtable), vsize) ||
        !contains(pos, tsize)) {
      ok_ = false;
      return {};
    }
    return {pos, static_cast<std::size_t>(vtable), vsize, tsize, true};
  }
  std::size_t slot(const Table& t, int id, std::size_t width) {
    if (!ok_ || !t.present) return 0;
    const std::size_t entry = 4 + 2 * static_cast<std::size_t>(id);
    if (entry + 2 > t.vsize) return 0;
    uint16_t offset = 0;
    if (!read(t.vtable + entry, offset) || offset == 0) return 0;
    if (offset + width > t.tsize) {
      ok_ = false;
      return 0;
    }
    return t.pos + offset;
  }
  Vector vectorAt(std::size_t pos, std::size_t width) {
    uint32_t count = 0;
    if (!ok_ || !read(pos, count)) return {};
    if (count > (size_ - pos - 4) / width) {
      ok_ = false;
      return {};
    }
    return {pos + 4, count};
  }

  const uint8_t* data_;
  std::size_t size_;
  bool ok_ = true;
};

}
