#pragma once

namespace awtrix {
namespace audio {

constexpr int kFftSize = 1024;

// Fills cosTab/sinTab[n/2] with e^(-2*pi*i*k/n). Built once; the tables live in the caller's scratch.
void fftTwiddles(float* cosTab, float* sinTab, int n);

// In-place radix-2 decimation in time, n a power of two, unnormalised: a full-scale bin-centred
// cosine answers n/2 in its bin. No stack arrays, so it is safe on a small task stack.
void fftInPlace(float* re, float* im, int n, const float* cosTab, const float* sinTab);

}
}
