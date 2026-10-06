#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechTokens.h"
#include "platform/tc002/speech/SpeechWave.h"

// One utterance's run of a voice, a chunk at a time: the acoustic model turns a chunk's tokens into
// mel frames and the vocoder those into samples, both a few frames per step. Every layer that
// looks at neighbouring frames keeps just the rows it still needs, so memory does not grow with
// the chunk, and the results equal those of the whole chunk at once.
namespace awtrix::speech {

// Frames each acoustic step regulates.
constexpr std::size_t kStepFrames = 16;

// Work space of the layers, shared by the acoustic model and the vocoder of one utterance.
struct SpeechScratch {
  explicit SpeechScratch(const SpeechModel& model);

  // Rows a layer works on at once; longer inputs go through in batches.
  std::size_t rows;
  std::size_t width;
  std::size_t hidden;
  std::size_t stride;
  std::vector<float> input;     // [rows][width]: depthwise results, gathered convolution inputs
  std::vector<float> expanded;  // [rows][hidden]
  std::vector<int8_t> codes;    // [rows][stride]
  std::vector<float> scales;    // [rows]
};

// The input rows of a layer that reads `radius` frames back and ahead: frames [first, first + held)
// of a chunk of `frames`, with zero rows standing in before the first frame and after the last.
class FrameWindow {
 public:
  void reserve(std::size_t rows, std::size_t width);
  void begin(std::size_t frames, std::size_t width, std::size_t radius);
  // Room for the next count frames; once the chunk's last frame is in, the zero rows follow.
  float* append(std::size_t count);
  // The frames whose output can be computed now, from next() on.
  std::size_t ready() const;
  std::size_t next() const { return next_; }
  bool done() const { return next_ == frames_; }
  const float* row(std::ptrdiff_t frame) const { return rows_.data() + (frame - first_) * width_; }
  // count outputs are done; rows no later output reads are dropped.
  void consume(std::size_t count);

 private:
  std::vector<float> rows_;
  std::size_t width_ = 0;
  std::size_t radius_ = 0;
  std::size_t frames_ = 0;
  std::size_t received_ = 0;
  std::size_t held_ = 0;
  std::size_t next_ = 0;
  std::ptrdiff_t first_ = 0;
};

// Tokens to normalised log-mel frames.
class AcousticStream {
 public:
  AcousticStream(const SpeechModel& model, SpeechScratch& scratch);

  // The most frames one next() writes.
  std::size_t maxFrames() const { return kStepFrames + lookahead_; }

  // Encoder and predictors over a chunk of at most kMaxChunkPhones + 1 tokens.
  void begin(const Token* tokens, std::size_t count);
  const uint8_t* durations() const { return durations_.data(); }
  const float* pitch() const { return pitch_.data(); }
  std::size_t frames() const { return frames_; }
  // The next mel frames of the chunk, [count][mels]; none while the decoder fills up.
  std::size_t next(float* mel);
  bool done() const { return windows_.back().done(); }

 private:
  void predict(const PredictorLayers& layers, float* out);

  const SpeechModel& model_;
  SpeechScratch& scratch_;
  std::size_t lookahead_ = 0;
  std::size_t tokens_ = 0;
  std::size_t frames_ = 0;
  std::size_t phone_ = 0;
  std::size_t repeat_ = 0;
  std::size_t emitted_ = 0;
  FrameWindow phonesA_, phonesB_;
  std::vector<float> encoded_;
  std::vector<float> logDurations_;
  std::vector<float> pitch_;
  std::vector<uint8_t> durations_;
  std::vector<FrameWindow> windows_;
  std::vector<float> last_;
};

// Normalised log-mel frames to int16 samples.
class VocoderStream {
 public:
  // maxInput: the most frames one push() takes.
  VocoderStream(const SpeechModel& model, SpeechScratch& scratch, std::size_t maxInput);

  // The most samples one push() writes.
  std::size_t maxSamples() const { return maxSamples_; }

  void begin(std::size_t frames);
  std::size_t push(const float* mel, std::size_t count, int16_t* out);
  bool done() const { return windows_.back().done(); }

 private:
  const SpeechModel& model_;
  SpeechScratch& scratch_;
  FrameWindow input_;
  std::vector<FrameWindow> windows_;
  std::vector<float> last_;
  std::vector<float> spectra_;
  SpeechWave wave_;
  std::size_t maxSamples_ = 0;
};

}
