#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/tc002/speech/SpeechModel.h"

// The arithmetic of a voice, as the model's reference runtime defines it: float32 throughout but
// the int8 dot products, every operation in the reference's order, so the results agree to the bit
// where the C library's erf, exp and log1p do.
namespace awtrix::speech::kernels {

// The int32 sum of a[i] * b[i] for i < count, a multiple of 16. No value is -128.
int32_t dot(const int8_t* a, const int8_t* b, std::size_t count);

// x[0..count) as int8 codes with one scale: s = max|x| / 127 (1 when all are 0), codes
// round(x / s) half away from zero, clamped to +-127, zeros up to stride. Returns s.
float quantize(const float* x, std::size_t count, int8_t* codes, std::size_t stride);

// layer applied to `frames` input vectors x[t * xStride + i], written to y[t * yStride + o]. codes
// holds frames * layer.stride bytes, scales frames floats.
void dense(const DenseLayer& layer, const float* x, std::size_t xStride, std::size_t frames, float* y,
           std::size_t yStride, int8_t* codes, float* scales);

// numpy's float32 sum: eight interleaved partial sums, pairwise above 128 values.
float pairwiseSum(const float* x, std::size_t count);
// Per frame of `width` values at `stride`: (x - mean) / sqrt(variance + 1e-6) * gain + bias, the
// variance biased.
void layerNorm(const NormLayer& norm, float* x, std::size_t width, std::size_t frames, std::size_t stride);
void gelu(float* x, std::size_t count);
void relu(float* x, std::size_t count);

}
