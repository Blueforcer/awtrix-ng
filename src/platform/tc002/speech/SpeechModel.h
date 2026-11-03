#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix::posix {
class MappedFile;
}

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

// A voice file: an acoustic model from tokens to normalised log-mel frames and a vocoder from those
// to magnitudes and phases of an inverse STFT. ATTS v2, a voice pack, also carries its name and
// language and the text frontend the model was trained on. load() checks the whole file once; the
// model never changes afterwards, so any number of utterances read it at the same time.
// A loaded model reads its file in place, which takes flash time when its pages have left the page
// cache, so only threads that may wait read it: the loop thread reads the frontend's own copy, and an
// utterance holds the weights in memory while it speaks.
class SpeechModel {
 public:
  // Phone embedding rows of token contract 1, the English frontend of ATTS v1 files.
  static constexpr std::size_t kV1Phones = 41;
  static constexpr std::size_t kMaxPhones = 64;
  static constexpr std::size_t kStresses = 3;
  static constexpr std::size_t kWordEnds = 2;
  static constexpr std::size_t kPunctuations = 8;
  static constexpr uint32_t kSampleRate = 24000;
  static constexpr std::size_t kMaxFileBytes = 16u << 20;

  ~SpeechModel();

  // nullptr and a short reason when the file is missing, unreadable or not a voice this firmware
  // can run. The model keeps the file it loaded mapped, even after it was replaced or removed.
  static std::shared_ptr<const SpeechModel> load(const std::string& path, std::string& error);
  // The same for a file in memory, which the model copies.
  static std::shared_ptr<const SpeechModel> parse(const uint8_t* data, std::size_t size, std::string& error);

  // Reads the file's weights back if they have left the page cache and keeps them in memory for as
  // long as it lives, so speech under way never waits for flash: writing a file there can take it
  // longer than the ring holds. Where the system refuses, the model just reads on as it does.
  class Hold {
   public:
    explicit Hold(const SpeechModel& model);
    ~Hold();
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;

   private:
    const SpeechModel& model_;
  };

  uint32_t version = 0;
  uint32_t sampleRate = 0;
  std::size_t phoneCount = 0;
  // v2 only, else empty: pack.info (JSON), fe.text and fe.lexicon.
  std::string_view info, frontendText, frontendLexicon;
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
  bool resolve(const uint8_t* file, std::size_t size, uint32_t checksum, std::string& error);

  // The whole file, one or the other; every pointer above points into it. A page-aligned mapping
  // and these blocks both hold the 16-byte alignment of the file's tensor offsets.
  std::shared_ptr<const posix::MappedFile> mapped_;
  std::vector<Block16> storage_;
  mutable std::mutex holdMutex_;
  mutable std::size_t holds_ = 0;
};

uint32_t crc32(const uint8_t* data, std::size_t size);
// The checksum of a whole file behind a descriptor, its checksum field counted as zero. False, with
// errno, when the file cannot be read to `size` bytes.
bool fileChecksum(int fd, std::size_t size, uint32_t& out);

}
