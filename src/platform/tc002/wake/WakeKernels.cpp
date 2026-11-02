// The int8 kernels follow TensorFlow Lite's reference kernels (tensorflow/lite/kernels, Copyright The
// TensorFlow Authors, Apache-2.0).
#include "platform/tc002/wake/WakeKernels.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace awtrix::tc002::wake::kernels {
namespace {

int32_t doublingHighMul(int32_t a, int32_t b) {
  const bool overflow = a == b && a == std::numeric_limits<int32_t>::min();
  const int64_t product = int64_t{a} * b;
  const int32_t nudge = product >= 0 ? (1 << 30) : (1 - (1 << 30));
  const int32_t high = static_cast<int32_t>((product + nudge) / (int64_t{1} << 31));
  return overflow ? std::numeric_limits<int32_t>::max() : high;
}

int32_t roundingDivideByPot(int32_t x, int exponent) {
  const int32_t mask = static_cast<int32_t>((int64_t{1} << exponent) - 1);
  const int32_t remainder = x & mask;
  const int32_t threshold = (mask >> 1) + (x < 0 ? 1 : 0);
  return (x >> exponent) + (remainder > threshold ? 1 : 0);
}

int32_t multiply(int32_t x, const Scale& s) {
  const int left = s.shift > 0 ? s.shift : 0;
  const int right = s.shift > 0 ? 0 : -s.shift;
  return roundingDivideByPot(
    doublingHighMul(static_cast<int32_t>(static_cast<uint32_t>(x) << left), s.multiplier), right);
}

int32_t wrapAdd(int32_t a, int32_t b) {
  return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

std::array<std::size_t, kMaxRank> stridesOf(const Shape6& s) {
  std::array<std::size_t, kMaxRank> strides{};
  std::size_t step = 1;
  for (std::size_t i = kMaxRank; i-- > 0;) {
    strides[i] = step;
    step *= static_cast<std::size_t>(s.dims[i]);
  }
  return strides;
}

int32_t clampTo(int32_t value, const Node& n) { return std::min(n.actMax, std::max(n.actMin, value)); }

}

void conv(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& f = ts[n.in[1]];
  const Tensor& out = ts[n.out[0]];
  const int inH = in.dim(1), inW = in.dim(2), inC = in.dim(3);
  const int outH = out.dim(1), outW = out.dim(2), outC = out.dim(3);
  const int kH = f.dim(1), kW = f.dim(2), kC = f.dim(3);
  const int perGroup = outC / n.groups;
  const int8_t* input = in.i8();
  const int8_t* filter = f.i8();
  int8_t* output = out.o8();
  for (int b = 0; b < in.dim(0); ++b) {
    for (int oy = 0; oy < outH; ++oy) {
      for (int ox = 0; ox < outW; ++ox) {
        for (int oc = 0; oc < outC; ++oc) {
          const int group = oc / perGroup;
          int32_t acc = 0;
          for (int fy = 0; fy < kH; ++fy) {
            const int iy = oy * n.strideH - n.padH + n.dilationH * fy;
            if (iy < 0 || iy >= inH) continue;
            for (int fx = 0; fx < kW; ++fx) {
              const int ix = ox * n.strideW - n.padW + n.dilationW * fx;
              if (ix < 0 || ix >= inW) continue;
              const int8_t* x =
                input + ((static_cast<std::size_t>(b) * inH + iy) * inW + ix) * inC + group * kC;
              const int8_t* w = filter + ((static_cast<std::size_t>(oc) * kH + fy) * kW + fx) * kC;
              for (int c = 0; c < kC; ++c) acc += int32_t{w[c]} * (int32_t{x[c]} + n.inOffset);
            }
          }
          if (!n.bias.empty()) acc = wrapAdd(acc, n.bias[oc]);
          acc = wrapAdd(multiply(acc, n.channelScales[oc]), n.outOffset);
          output[((static_cast<std::size_t>(b) * outH + oy) * outW + ox) * outC + oc] =
            static_cast<int8_t>(clampTo(acc, n));
        }
      }
    }
  }
}

void depthwise(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& f = ts[n.in[1]];
  const Tensor& out = ts[n.out[0]];
  const int inH = in.dim(1), inW = in.dim(2), inC = in.dim(3);
  const int outH = out.dim(1), outW = out.dim(2), outC = out.dim(3);
  const int kH = f.dim(1), kW = f.dim(2);
  const int8_t* input = in.i8();
  const int8_t* filter = f.i8();
  int8_t* output = out.o8();
  for (int b = 0; b < in.dim(0); ++b) {
    for (int oy = 0; oy < outH; ++oy) {
      for (int ox = 0; ox < outW; ++ox) {
        for (int ic = 0; ic < inC; ++ic) {
          for (int m = 0; m < n.depthMultiplier; ++m) {
            const int oc = m + ic * n.depthMultiplier;
            int32_t acc = 0;
            for (int fy = 0; fy < kH; ++fy) {
              const int iy = oy * n.strideH - n.padH + n.dilationH * fy;
              if (iy < 0 || iy >= inH) continue;
              for (int fx = 0; fx < kW; ++fx) {
                const int ix = ox * n.strideW - n.padW + n.dilationW * fx;
                if (ix < 0 || ix >= inW) continue;
                const int32_t x = input[((static_cast<std::size_t>(b) * inH + iy) * inW + ix) * inC + ic];
                const int32_t w = filter[(static_cast<std::size_t>(fy) * kW + fx) * outC + oc];
                acc += w * (x + n.inOffset);
              }
            }
            if (!n.bias.empty()) acc = wrapAdd(acc, n.bias[oc]);
            acc = wrapAdd(multiply(acc, n.channelScales[oc]), n.outOffset);
            output[((static_cast<std::size_t>(b) * outH + oy) * outW + ox) * outC + oc] =
              static_cast<int8_t>(clampTo(acc, n));
          }
        }
      }
    }
  }
}

void fullyConnected(const std::vector<Tensor>& ts, const Node& n) {
  const int8_t* input = ts[n.in[0]].i8();
  const int8_t* weights = ts[n.in[1]].i8();
  int8_t* output = ts[n.out[0]].o8();
  const bool perChannel = n.realScales.size() > 1;
  for (std::size_t b = 0; b < n.outer; ++b) {
    const int8_t* x = input + b * n.inner;
    for (int u = 0; u < n.units; ++u) {
      const int8_t* w = weights + static_cast<std::size_t>(u) * n.inner;
      int32_t acc = 0;
      for (std::size_t d = 0; d < n.inner; ++d)
        acc += (int32_t{w[d]} + n.filterOffset) * (int32_t{x[d]} + n.inOffset);
      if (!n.bias.empty()) acc = wrapAdd(acc, n.bias[u]);
      double scaled = std::round(static_cast<double>(acc) * n.realScales[perChannel ? u : 0]);
      scaled = std::max(-2147483648.0, std::min(2147483647.0, scaled));
      const int32_t value = wrapAdd(static_cast<int32_t>(scaled), n.outOffset);
      output[b * n.units + u] = static_cast<int8_t>(clampTo(value, n));
    }
  }
}

void averagePool(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& out = ts[n.out[0]];
  const int inH = in.dim(1), inW = in.dim(2), depth = in.dim(3);
  const int8_t* input = in.i8();
  int8_t* output = out.o8();
  for (int b = 0; b < in.dim(0); ++b) {
    for (int oy = 0; oy < out.dim(1); ++oy) {
      for (int ox = 0; ox < out.dim(2); ++ox) {
        const int x0 = ox * n.strideW - n.padW, y0 = oy * n.strideH - n.padH;
        const int fxStart = std::max(0, -x0), fxEnd = std::min(n.filterW, inW - x0);
        const int fyStart = std::max(0, -y0), fyEnd = std::min(n.filterH, inH - y0);
        for (int c = 0; c < depth; ++c) {
          int32_t acc = 0, count = 0;
          for (int fy = fyStart; fy < fyEnd; ++fy) {
            for (int fx = fxStart; fx < fxEnd; ++fx) {
              const std::size_t at = (static_cast<std::size_t>(b) * inH + y0 + fy) * inW + x0 + fx;
              acc += input[at * depth + c];
              ++count;
            }
          }
          const int32_t value = acc > 0 ? (acc + count / 2) / count : (acc - count / 2) / count;
          output[((static_cast<std::size_t>(b) * out.dim(1) + oy) * out.dim(2) + ox) * depth + c] =
            static_cast<int8_t>(clampTo(value, n));
        }
      }
    }
  }
}

void stridedSlice(const std::vector<Tensor>& ts, const Node& n) {
  const uint8_t* in = ts[n.in[0]].data;
  uint8_t* out = ts[n.out[0]].data;
  const std::size_t width = ts[n.in[0]].bytes / ts[n.in[0]].elements;
  const auto s = stridesOf(n.shape1);
  std::size_t o = 0;
  for (int32_t a = 0; a < n.count[0]; ++a)
    for (int32_t b = 0; b < n.count[1]; ++b)
      for (int32_t c = 0; c < n.count[2]; ++c)
        for (int32_t d = 0; d < n.count[3]; ++d)
          for (int32_t e = 0; e < n.count[4]; ++e)
            for (int32_t f = 0; f < n.count[5]; ++f) {
              const std::array<int32_t, kMaxRank> at = {a, b, c, d, e, f};
              std::size_t i = 0;
              for (std::size_t k = 0; k < kMaxRank; ++k)
                i += static_cast<std::size_t>(n.start[k] + at[k] * n.step[k]) * s[k];
              std::memcpy(out + o, in + i * width, width);
              o += width;
            }
}

void logistic(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  int8_t* out = ts[n.out[0]].o8();
  for (std::size_t i = 0; i < in.elements; ++i) out[i] = n.lut[static_cast<uint8_t>(in.i8()[i])];
}

void quantize(const std::vector<Tensor>& ts, const Node& n) {
  const Tensor& in = ts[n.in[0]];
  const Tensor& out = ts[n.out[0]];
  for (std::size_t i = 0; i < in.elements; ++i) {
    const int32_t x = in.type == kUInt8 ? int32_t{in.data[i]} : int32_t{in.i8()[i]};
    const int32_t value = clampTo(multiply(x - n.inOffset, n.scaleOut) + n.outOffset, n);
    out.data[i] = static_cast<uint8_t>(value);
  }
}

void concatenation(const std::vector<Tensor>& ts, const Node& n) {
  uint8_t* out = ts[n.out[0]].data;
  for (std::size_t k = 0; k < n.outer; ++k) {
    for (std::size_t i = 0; i < n.in.size(); ++i) {
      std::memcpy(out, ts[n.in[i]].data + k * n.parts[i], n.parts[i]);
      out += n.parts[i];
    }
  }
}

void splitV(const std::vector<Tensor>& ts, const Node& n) {
  const uint8_t* in = ts[n.in[0]].data;
  for (std::size_t k = 0; k < n.outer; ++k) {
    for (std::size_t i = 0; i < n.out.size(); ++i) {
      std::memcpy(ts[n.out[i]].data + k * n.parts[i], in, n.parts[i]);
      in += n.parts[i];
    }
  }
}

void copyToOutput(const std::vector<Tensor>& ts, const Node& n) {
  std::memcpy(ts[n.out[0]].data, ts[n.in[0]].data, ts[n.out[0]].bytes);
}

void assignVariable(const std::vector<Tensor>& ts, const Node& n) {
  std::memcpy(ts[n.in[0]].data, ts[n.in[1]].data, ts[n.in[1]].bytes);
}

}
