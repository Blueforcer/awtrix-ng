// A port of KISS FFT 1.3.0's real FFT (kiss_fftr, kf_work, kf_bfly4; Copyright (c) 2003-2010 Mark
// Borgerding, BSD-3-Clause) with FIXED_POINT 16, as TensorFlow Lite Micro's microfrontend builds it.
#include "platform/tc002/wake/WakeFft.h"

#include <array>
#include <cmath>

namespace awtrix::tc002::wake {
namespace {

int16_t narrow(int32_t v) { return static_cast<int16_t>(v); }
int16_t sround(int32_t v) { return narrow((v + (1 << 14)) >> 15); }

struct Twiddles {
  std::array<WakeFft::Cpx, WakeFft::kHalf> full;
  std::array<WakeFft::Cpx, WakeFft::kHalf / 2> super;
};

const Twiddles& twiddles() {
  static const Twiddles table = [] {
    Twiddles t{};
    constexpr int complexSize = static_cast<int>(WakeFft::kHalf);
    for (int i = 0; i < complexSize; ++i) {
      const double pi = 3.141592653589793238462643383279502884197169399375105820974944;
      const double phase = -2 * pi * i / complexSize;
      t.full[i] = {static_cast<int16_t>(std::floor(.5 + 32767 * std::cos(phase))),
                   static_cast<int16_t>(std::floor(.5 + 32767 * std::sin(phase)))};
    }
    for (int i = 0; i < complexSize / 2; ++i) {
      const double phase = -3.14159265358979323846264338327 * (static_cast<double>(i + 1) / complexSize + .5);
      t.super[i] = {static_cast<int16_t>(std::floor(.5 + 32767 * std::cos(phase))),
                    static_cast<int16_t>(std::floor(.5 + 32767 * std::sin(phase)))};
    }
    return t;
  }();
  return table;
}

}

void WakeFft::run(const Input& input, Spectrum& spectrum) {
  std::array<Cpx, kHalf> pairs;
  for (std::size_t k = 0; k < kHalf; ++k) pairs[k] = {input[2 * k], input[2 * k + 1]};
  work(temp_.data(), pairs.data(), 1, kHalf / 4);

  auto fixdiv2 = [](Cpx& c) {
    c.r = sround(int32_t{c.r} * (32767 / 2));
    c.i = sround(int32_t{c.i} * (32767 / 2));
  };
  Cpx dc = temp_[0];
  fixdiv2(dc);
  spectrum[0] = {narrow(dc.r + dc.i), 0};
  spectrum[kHalf] = {narrow(dc.r - dc.i), 0};
  const auto& super = twiddles().super;
  for (std::size_t k = 1; k <= kHalf / 2; ++k) {
    Cpx fpk = temp_[k];
    Cpx fpnk = {temp_[kHalf - k].r, narrow(-temp_[kHalf - k].i)};
    fixdiv2(fpk);
    fixdiv2(fpnk);
    const Cpx f1k = {narrow(fpk.r + fpnk.r), narrow(fpk.i + fpnk.i)};
    const Cpx f2k = {narrow(fpk.r - fpnk.r), narrow(fpk.i - fpnk.i)};
    const Cpx& tw = super[k - 1];
    const Cpx t = {sround(f2k.r * tw.r - f2k.i * tw.i), sround(f2k.r * tw.i + f2k.i * tw.r)};
    spectrum[k] = {narrow((f1k.r + t.r) >> 1), narrow((f1k.i + t.i) >> 1)};
    spectrum[kHalf - k] = {narrow((f1k.r - t.r) >> 1), narrow((t.i - f1k.i) >> 1)};
  }
}

void WakeFft::work(Cpx* out, const Cpx* in, std::size_t stride, std::size_t m) const {
  if (m == 1) {
    for (std::size_t k = 0; k < 4; ++k) out[k] = in[k * stride];
  } else {
    for (std::size_t k = 0; k < 4; ++k) work(out + k * m, in + k * stride, stride * 4, m / 4);
  }
  butterfly(out, stride, m);
}

void WakeFft::butterfly(Cpx* out, std::size_t stride, std::size_t m) const {
  auto fixdiv4 = [](Cpx& c) {
    c.r = sround(int32_t{c.r} * (32767 / 4));
    c.i = sround(int32_t{c.i} * (32767 / 4));
  };
  auto mul = [](const Cpx& a, const Cpx& b) {
    return Cpx{sround(a.r * b.r - a.i * b.i), sround(a.r * b.i + a.i * b.r)};
  };
  auto add = [](const Cpx& a, const Cpx& b) { return Cpx{narrow(a.r + b.r), narrow(a.i + b.i)}; };
  auto sub = [](const Cpx& a, const Cpx& b) { return Cpx{narrow(a.r - b.r), narrow(a.i - b.i)}; };
  const Cpx* tw1 = twiddles().full.data();
  const Cpx* tw2 = tw1;
  const Cpx* tw3 = tw1;
  for (std::size_t k = 0; k < m; ++k) {
    Cpx* f = out + k;
    fixdiv4(f[0]);
    fixdiv4(f[m]);
    fixdiv4(f[2 * m]);
    fixdiv4(f[3 * m]);
    const Cpx s0 = mul(f[m], *tw1), s1 = mul(f[2 * m], *tw2), s2 = mul(f[3 * m], *tw3);
    const Cpx s5 = sub(f[0], s1);
    f[0] = add(f[0], s1);
    const Cpx s3 = add(s0, s2), s4 = sub(s0, s2);
    f[2 * m] = sub(f[0], s3);
    tw1 += stride;
    tw2 += 2 * stride;
    tw3 += 3 * stride;
    f[0] = add(f[0], s3);
    f[m] = {narrow(s5.r + s4.i), narrow(s5.i - s4.r)};
    f[3 * m] = {narrow(s5.r - s4.i), narrow(s5.i + s4.r)};
  }
}

}
