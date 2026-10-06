#include "platform/tc002/speech/SpeechKernels.h"

#include <algorithm>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define AWTRIX_SPEECH_NEON 1
#endif

namespace awtrix::speech::kernels {
namespace {

#ifdef AWTRIX_SPEECH_NEON
// Two products per int16 lane: with both factors within +-127 their sum stays within +-32258.
inline int32x4_t accumulate(int32x4_t sum, int8x16_t x, int8x8_t low, int8x8_t high) {
  return vpadalq_s16(sum, vmlal_s8(vmull_s8(vget_low_s8(x), low), vget_high_s8(x), high));
}

inline int32_t total(int32x4_t sum) {
  const int32x2_t half = vadd_s32(vget_low_s32(sum), vget_high_s32(sum));
  return vget_lane_s32(vpadd_s32(half, half), 0);
}
#endif

// One weight row against four input vectors `stride` apart.
void dot4(const int8_t* w, const int8_t* x, std::size_t stride, int32_t* out) {
#ifdef AWTRIX_SPEECH_NEON
  int32x4_t a = vdupq_n_s32(0), b = a, c = a, d = a;
  for (std::size_t i = 0; i < stride; i += 16) {
    const int8x16_t weights = vld1q_s8(w + i);
    const int8x8_t low = vget_low_s8(weights), high = vget_high_s8(weights);
    a = accumulate(a, vld1q_s8(x + i), low, high);
    b = accumulate(b, vld1q_s8(x + stride + i), low, high);
    c = accumulate(c, vld1q_s8(x + 2 * stride + i), low, high);
    d = accumulate(d, vld1q_s8(x + 3 * stride + i), low, high);
  }
  out[0] = total(a);
  out[1] = total(b);
  out[2] = total(c);
  out[3] = total(d);
#else
  for (std::size_t k = 0; k < 4; ++k) out[k] = dot(w, x + k * stride, stride);
#endif
}

}

int32_t dot(const int8_t* a, const int8_t* b, std::size_t count) {
#ifdef AWTRIX_SPEECH_NEON
  int32x4_t sum = vdupq_n_s32(0);
  for (std::size_t i = 0; i < count; i += 16) {
    const int8x16_t weights = vld1q_s8(b + i);
    sum = accumulate(sum, vld1q_s8(a + i), vget_low_s8(weights), vget_high_s8(weights));
  }
  return total(sum);
#else
  int32_t sum = 0;
  for (std::size_t i = 0; i < count; ++i) sum += int32_t{a[i]} * int32_t{b[i]};
  return sum;
#endif
}

float quantize(const float* x, std::size_t count, int8_t* codes, std::size_t stride) {
  float peak = 0.0f;
  for (std::size_t i = 0; i < count; ++i) peak = std::max(peak, std::fabs(x[i]));
  const float scale = peak > 0.0f ? peak / 127.0f : 1.0f;
  const float reciprocal = 1.0f / scale;
  for (std::size_t i = 0; i < count; ++i) {
    const float value = x[i] * reciprocal;
    const float rounded = std::floor(std::fabs(value) + 0.5f);
    // Also NaN ends up at the limit rather than in an undefined conversion.
    const float magnitude = rounded < 127.0f ? rounded : 127.0f;
    codes[i] = static_cast<int8_t>(value < 0.0f ? -magnitude : magnitude);
  }
  std::fill(codes + count, codes + stride, int8_t{0});
  return scale;
}

void dense(const DenseLayer& layer, const float* x, std::size_t xStride, std::size_t frames, float* y,
           std::size_t yStride, int8_t* codes, float* scales) {
  const std::size_t stride = layer.stride;
  for (std::size_t t = 0; t < frames; ++t) scales[t] = quantize(x + t * xStride, layer.cols, codes + t * stride, stride);
  for (std::size_t o = 0; o < layer.rows; ++o) {
    const int8_t* weights = layer.weights + o * stride;
    const float scale = layer.scales[o], bias = layer.biases[o];
    std::size_t t = 0;
    for (; t + 4 <= frames; t += 4) {
      int32_t sums[4];
      dot4(weights, codes + t * stride, stride, sums);
      for (std::size_t k = 0; k < 4; ++k)
        y[(t + k) * yStride + o] = scales[t + k] * scale * static_cast<float>(sums[k]) + bias;
    }
    for (; t < frames; ++t)
      y[t * yStride + o] = scales[t] * scale * static_cast<float>(dot(weights, codes + t * stride, stride)) + bias;
  }
}

float pairwiseSum(const float* x, std::size_t count) {
  if (count < 8) {
    float sum = 0.0f;
    for (std::size_t i = 0; i < count; ++i) sum += x[i];
    return sum;
  }
  if (count <= 128) {
    float r[8];
    std::copy_n(x, 8, r);
    std::size_t i = 8;
    for (; i < count - count % 8; i += 8)
      for (std::size_t k = 0; k < 8; ++k) r[k] += x[i + k];
    float sum = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < count; ++i) sum += x[i];
    return sum;
  }
  std::size_t half = count / 2;
  half -= half % 8;
  return pairwiseSum(x, half) + pairwiseSum(x + half, count - half);
}

void layerNorm(const NormLayer& norm, float* x, std::size_t width, std::size_t frames, std::size_t stride) {
  const auto inverseWidth = static_cast<float>(1.0 / static_cast<double>(width));
  float squares[1024];
  for (std::size_t t = 0; t < frames; ++t) {
    float* row = x + t * stride;
    const float mean = pairwiseSum(row, width) * inverseWidth;
    for (std::size_t c = 0; c < width; ++c) {
      row[c] -= mean;
      squares[c] = row[c] * row[c];
    }
    const float variance = pairwiseSum(squares, width) * inverseWidth;
    const float inverse = 1.0f / std::sqrt(variance + 1e-6f);
    for (std::size_t c = 0; c < width; ++c) row[c] = row[c] * inverse * norm.gains[c] + norm.biases[c];
  }
}

void gelu(float* x, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    const float v = x[i];
    const auto e = static_cast<float>(std::erf(static_cast<double>(v * 0.70710678118654752f)));
    x[i] = 0.5f * v * (1.0f + e);
  }
}

void relu(float* x, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) x[i] = std::max(x[i], 0.0f);
}

}
