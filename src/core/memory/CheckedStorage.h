#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace awtrix::checked {

namespace storage {
// Allocation hooks for checked strings, arrays and shared ownership.
// Owners capture their release function before a hook is changed.
struct Allocator {
  void* (*allocate)(std::size_t) = std::malloc;
  void (*release)(void*) = std::free;
};
inline Allocator& allocator() { static Allocator value; return value; }
inline void setAllocator(Allocator value) { allocator() = value; }

template <typename T, typename = void> struct HasValid : std::false_type {};
template <typename T> struct HasValid<T, std::void_t<decltype(std::declval<const T&>().valid())>>
    : std::true_type {};
template <typename T> bool valid(const T& value) {
  if constexpr (HasValid<T>::value) return value.valid();
  return true;
}
}

// Heap failure leaves existing bytes intact and marks the value invalid. A complete successful
// assignment or clear() starts a new valid value.
class CheckedString {
 public:
  CheckedString() = default;
  CheckedString(std::string_view value) { assign(value); }
  CheckedString(const char* value) { assign(value ? std::string_view(value) : std::string_view{}); }
  CheckedString(const CheckedString& other) { assign(other.view()); valid_ &= other.valid_; }
  CheckedString(CheckedString&& other) noexcept { take(other); }
  ~CheckedString() { releaseBuffer(); }

  CheckedString& operator=(const CheckedString& other) {
    if (this != &other) { assign(other.view()); valid_ &= other.valid_; }
    return *this;
  }
  CheckedString& operator=(CheckedString&& other) noexcept {
    if (this != &other) { releaseBuffer(); take(other); }
    return *this;
  }
  CheckedString& operator=(std::string_view value) { assign(value); return *this; }
  CheckedString& operator=(const char* value) {
    assign(value ? std::string_view(value) : std::string_view{}); return *this;
  }

  bool assign(std::string_view value) {
    if (!reserve(value.size())) return false;
    if (!value.empty()) std::memmove(data(), value.data(), value.size());
    size_ = value.size();
    data()[size_] = '\0';
    valid_ = true;
    return true;
  }
  bool assign(const CheckedString& value) {
    if (this == &value) return valid_;
    const bool copied = assign(value.view());
    valid_ &= value.valid_;
    return copied && valid_;
  }
  bool assign(const char* value) {
    return assign(value ? std::string_view(value) : std::string_view{});
  }
  bool assign(const char* value, std::size_t size) { return assign(std::string_view(value, size)); }
  bool reserve(std::size_t capacity) {
    if (capacity <= capacity_) return true;
    if (capacity == std::numeric_limits<std::size_t>::max()) return valid_ = false;
    const auto hook = storage::allocator();
    char* next = static_cast<char*>(hook.allocate(capacity + 1));
    if (!next) return valid_ = false;
    std::memcpy(next, data(), size_ + 1);
    releaseBuffer();
    heap_ = next;
    release_ = hook.release;
    capacity_ = capacity;
    return true;
  }
  bool resize(std::size_t size, char value = '\0') {
    if (!reserve(size)) return false;
    if (size > size_) std::memset(data() + size_, value, size - size_);
    size_ = size;
    data()[size_] = '\0';
    return valid_;
  }
  bool push_back(char value) {
    if (size_ >= std::numeric_limits<std::size_t>::max() - 1) return valid_ = false;
    if (size_ == capacity_) {
      const std::size_t max = std::numeric_limits<std::size_t>::max() - 1;
      const std::size_t capacity = capacity_ <= max / 2 ? capacity_ * 2 : max;
      if (!reserve(capacity)) return false;
    }
    data()[size_++] = value;
    data()[size_] = '\0';
    return valid_;
  }
  void clear() { size_ = 0; data()[0] = '\0'; valid_ = true; }
  bool valid() const { return valid_; }
  bool empty() const { return size_ == 0; }
  std::size_t size() const { return size_; }
  std::size_t capacity() const { return capacity_; }
  char* data() { return heap_ ? heap_ : local_; }
  const char* data() const { return heap_ ? heap_ : local_; }
  const char* c_str() const { return data(); }
  char* begin() { return data(); }
  char* end() { return data() + size_; }
  const char* begin() const { return data(); }
  const char* end() const { return data() + size_; }
  char& operator[](std::size_t index) { return data()[index]; }
  const char& operator[](std::size_t index) const { return data()[index]; }
  std::string_view view() const { return {data(), size_}; }
  operator std::string_view() const { return view(); }

  friend bool operator==(const CheckedString& a, const CheckedString& b) { return a.view() == b.view(); }
  friend bool operator!=(const CheckedString& a, const CheckedString& b) { return !(a == b); }
  friend bool operator==(const CheckedString& a, std::string_view b) { return a.view() == b; }
  friend bool operator!=(const CheckedString& a, std::string_view b) { return !(a == b); }
  friend bool operator==(std::string_view a, const CheckedString& b) { return a == b.view(); }
  friend bool operator!=(std::string_view a, const CheckedString& b) { return !(a == b); }
  friend bool operator==(const CheckedString& a, const char* b) { return a.view() == b; }
  friend bool operator!=(const CheckedString& a, const char* b) { return !(a == b); }
  friend bool operator==(const char* a, const CheckedString& b) { return a == b.view(); }
  friend bool operator!=(const char* a, const CheckedString& b) { return !(a == b); }

 private:
  void releaseBuffer() {
    if (heap_) release_(heap_);
    heap_ = nullptr;
    release_ = nullptr;
  }
  void take(CheckedString& other) {
    size_ = other.size_; capacity_ = other.capacity_; valid_ = other.valid_;
    heap_ = other.heap_; release_ = other.release_;
    if (!heap_) std::memcpy(local_, other.local_, size_ + 1);
    other.heap_ = nullptr; other.release_ = nullptr;
    other.size_ = 0; other.capacity_ = kLocalCapacity; other.valid_ = true;
    other.local_[0] = '\0';
  }
  static constexpr std::size_t kLocalCapacity = 31;
  char local_[kLocalCapacity + 1] = {};
  char* heap_ = nullptr;
  void (*release_)(void*) = nullptr;
  std::size_t size_ = 0, capacity_ = kLocalCapacity;
  bool valid_ = true;
};

// T must itself have a nonthrowing move and checked copy semantics. valid() includes an
// element's valid() when available, so nested layout values cannot hide a failed string copy.
template <typename T> class CheckedArray {
  static_assert(std::is_nothrow_move_constructible<T>::value, "checked arrays need nonthrowing moves");
  static_assert(std::is_trivially_copyable<T>::value || storage::HasValid<T>::value,
                "checked arrays need POD or values with checked copy semantics");
 public:
  CheckedArray() = default;
  CheckedArray(std::initializer_list<T> values) { assign(values.begin(), values.end()); }
  CheckedArray(const CheckedArray& other) {
    assign(other.begin(), other.end()); valid_ &= other.valid();
  }
  CheckedArray(CheckedArray&& other) noexcept { take(other); }
  ~CheckedArray() { releaseBuffer(); }
  CheckedArray& operator=(const CheckedArray& other) {
    if (this != &other) {
      CheckedArray next(other);
      if (next.valid()) *this = std::move(next);
      else valid_ = false;
    }
    return *this;
  }
  CheckedArray& operator=(CheckedArray&& other) noexcept {
    if (this != &other) { releaseBuffer(); take(other); }
    return *this;
  }
  CheckedArray& operator=(std::initializer_list<T> values) {
    assign(values.begin(), values.end()); return *this;
  }
  bool assign(const T* first, const T* last) {
    CheckedArray next;
    const std::size_t count = first == last ? 0 : static_cast<std::size_t>(last - first);
    if (!next.reserve(count)) return valid_ = false;
    for (std::size_t i = 0; i < count; ++i)
      if (!next.push_back(first[i])) return valid_ = false;
    *this = std::move(next);
    return true;
  }
  bool reserve(std::size_t capacity) {
    if (capacity <= capacity_) return true;
    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(T)) return valid_ = false;
    const auto hook = storage::allocator();
    T* next = static_cast<T*>(hook.allocate(capacity * sizeof(T)));
    if (!next) return valid_ = false;
    for (std::size_t i = 0; i < size_; ++i) new (&next[i]) T(std::move(data_[i]));
    for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
    if (data_) release_(data_);
    data_ = next; release_ = hook.release; capacity_ = capacity;
    return true;
  }
  template <typename... Args> bool emplace_back(Args&&... args) {
    // Constructs the element before the array grows.
    T next(std::forward<Args>(args)...);
    if (!storage::valid(next)) return valid_ = false;
    if (!growForOne()) return false;
    new (&data_[size_++]) T(std::move(next));
    return valid_;
  }
  bool push_back(const T& value) { return emplace_back(value); }
  bool push_back(T&& value) { return emplace_back(std::move(value)); }
  bool resize(std::size_t size) {
    if (size <= size_) { while (size_ > size) pop_back(); return valid_; }
    if (!reserve(size)) return false;
    while (size_ < size) if (!emplace_back()) return false;
    return valid_;
  }
  bool resize(std::size_t size, const T& value) {
    if (size <= size_) { while (size_ > size) pop_back(); return valid_; }
    T saved(value);
    if (!storage::valid(saved) || !reserve(size)) return valid_ = false;
    while (size_ < size) if (!push_back(saved)) return false;
    return valid_;
  }
  void pop_back() { if (size_) data_[--size_].~T(); }
  void clear() { while (size_) pop_back(); valid_ = true; }
  bool valid() const {
    if (!valid_) return false;
    for (std::size_t i = 0; i < size_; ++i) if (!storage::valid(data_[i])) return false;
    return true;
  }
  bool empty() const { return size_ == 0; }
  std::size_t size() const { return size_; }
  std::size_t capacity() const { return capacity_; }
  T* data() { return data_; }
  const T* data() const { return data_; }
  T* begin() { return data_; }
  const T* begin() const { return data_; }
  T* end() { return size_ ? data_ + size_ : data_; }
  const T* end() const { return size_ ? data_ + size_ : data_; }
  T& front() { return data_[0]; }
  const T& front() const { return data_[0]; }
  T& back() { return data_[size_ - 1]; }
  const T& back() const { return data_[size_ - 1]; }
  T& operator[](std::size_t index) { return data_[index]; }
  const T& operator[](std::size_t index) const { return data_[index]; }

 private:
  bool growForOne() {
    if (size_ < capacity_) return true;
    const auto max = std::numeric_limits<std::size_t>::max() / sizeof(T);
    if (size_ == max) return valid_ = false;
    const std::size_t capacity = capacity_ ? (capacity_ <= max / 2 ? capacity_ * 2 : max) : 1;
    return reserve(capacity);
  }
  void releaseBuffer() {
    clear();
    if (data_) release_(data_);
    data_ = nullptr; release_ = nullptr; capacity_ = 0;
  }
  void take(CheckedArray& other) {
    data_ = other.data_; release_ = other.release_;
    size_ = other.size_; capacity_ = other.capacity_; valid_ = other.valid_;
    other.data_ = nullptr; other.release_ = nullptr;
    other.size_ = other.capacity_ = 0; other.valid_ = true;
  }
  T* data_ = nullptr;
  void (*release_)(void*) = nullptr;
  std::size_t size_ = 0, capacity_ = 0;
  bool valid_ = true;
};

}
