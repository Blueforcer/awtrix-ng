#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace awtrix::speech {

// An int8 layer: for an input vector quantised to q with its scale s, output o is
// (s * scales[o]) * sum(q[i] * weights[o * stride + i]) + biases[o]. Rows hold cols weights and
// zeros up to stride, a multiple of 16; no weight is -128.
struct DenseLayer {
  const int8_t* weights = nullptr;
  const float* scales = nullptr;
  const float* biases = nullptr;
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::size_t stride = 0;
};

struct NormLayer {
  const float* gains = nullptr;
  const float* biases = nullptr;
};

// A ConvNeXt block over `width` channels: depthwise convolution, norm, expand, GELU, project, and
// the input added back scaled by gamma.
struct BlockLayer {
  const float* depthwise = nullptr;  // [width][kernel]
  const float* depthwiseBiases = nullptr;
  NormLayer norm;
  DenseLayer expand;
  DenseLayer project;
  const float* gamma = nullptr;
  std::size_t width = 0;
  std::size_t kernel = 0;
  std::size_t dilation = 1;
};

// Two k3 convolutions with ReLU and norm, then one value per phone.
struct PredictorLayers {
  DenseLayer first;
  NormLayer firstNorm;
  DenseLayer second;
  NormLayer secondNorm;
  DenseLayer out;
};

// A voice file, ATTS v1: an acoustic model from tokens to normalised log-mel frames and a vocoder
// from those to magnitudes and phases of an inverse STFT. load() checks the whole file once; the
// model never changes afterwards, so any number of utterances read it at the same time.
class SpeechModel {
 public:
  // Embedding rows of token contract v1.
  static constexpr std::size_t kPhones = 41;
  static constexpr std::size_t kStresses = 3;
  static constexpr std::size_t kWordEnds = 2;
  static constexpr std::size_t kPunctuations = 8;
  static constexpr uint32_t kSampleRate = 24000;
  static constexpr std::size_t kMaxFileBytes = 16u << 20;

  // nullptr and a short reason when the file is missing, unreadable or not a voice this firmware
  // can run.
  static std::shared_ptr<const SpeechModel> load(const std::string& path, std::string& error);
  static std::shared_ptr<const SpeechModel> parse(const uint8_t* data, std::size_t size, std::string& error);

  uint32_t sampleRate = 0;
  std::size_t hop = 0;
  std::size_t fftSize = 0;
  std::size_t mels = 0;
  std::size_t maxDuration = 0;

  // Token embeddings, [rows][encoder width] each.
  const float* phones = nullptr;
  const float* stresses = nullptr;
  const float* wordEnds = nullptr;
  const float* punctuations = nullptr;
  std::vector<BlockLayer> encoder;
  NormLayer encoderNorm;
  PredictorLayers duration;
  PredictorLayers pitch;
  DenseLayer pitchEmbedding;
  DenseLayer frameIn;
  std::vector<BlockLayer> decoder;
  NormLayer decoderNorm;
  DenseLayer melOut;
  DenseLayer vocoderIn;
  std::size_t vocoderInKernel = 0;
  NormLayer vocoderInNorm;
  std::vector<BlockLayer> vocoder;
  NormLayer vocoderNorm;
  DenseLayer head;

  std::size_t encoderWidth() const { return frameIn.cols - 2; }
  std::size_t decoderWidth() const { return frameIn.rows; }
  std::size_t vocoderWidth() const { return vocoderIn.rows; }

 private:
  struct alignas(16) Block16 {
    uint8_t bytes[16];
  };

  SpeechModel() = default;
  bool resolve(std::size_t size, std::string& error);

  // The whole file; every pointer above points into it.
  std::vector<Block16> storage_;
};

uint32_t crc32(const uint8_t* data, std::size_t size);

}
