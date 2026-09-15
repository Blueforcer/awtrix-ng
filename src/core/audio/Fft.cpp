#include "core/audio/Fft.h"

#include <cmath>
#include <utility>

namespace awtrix {
namespace audio {

void fftTwiddles(float* cosTab, float* sinTab, int n) {
  const double step = -2.0 * 3.14159265358979323846 / n;
  for (int k = 0; k < n / 2; ++k) {
    cosTab[k] = static_cast<float>(std::cos(step * k));
    sinTab[k] = static_cast<float>(std::sin(step * k));
  }
}

void fftInPlace(float* re, float* im, int n, const float* cosTab, const float* sinTab) {
  for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      std::swap(re[i], re[j]);
      std::swap(im[i], im[j]);
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const int half = len >> 1;
    const int stride = n / len;
    for (int start = 0; start < n; start += len) {
      for (int k = 0; k < half; ++k) {
        const float wr = cosTab[k * stride];
        const float wi = sinTab[k * stride];
        const int a = start + k;
        const int b = a + half;
        const float tr = re[b] * wr - im[b] * wi;
        const float ti = re[b] * wi + im[b] * wr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
}

}
}
