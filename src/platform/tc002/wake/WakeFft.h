#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix::tc002::wake {

inline int16_t narrow(int32_t v) { return static_cast<int16_t>(v); }

class WakeFft {
 public:
  struct Cpx {
    int16_t r, i;
  };
  static constexpr std::size_t kSize = 512;
  static constexpr std::size_t kHalf = kSize / 2;
  using Input = std::array<int16_t, kSize>;
  using Spectrum = std::array<Cpx, kHalf + 1>;

  void run(const Input& input, Spectrum& spectrum);

 private:
  void work(Cpx* out, const Cpx* in, std::size_t stride, std::size_t m) const;
  void butterfly(Cpx* out, std::size_t stride, std::size_t m) const;

  std::array<Cpx, kHalf> temp_{};
};

}
