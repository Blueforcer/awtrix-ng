#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix::tc002::wake {

// KISS FFT's real FFT over 512 points in 16-bit fixed point, as the microfrontend runs it. The twiddle
// tables are the same for every instance and computed once. Never allocates.
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
