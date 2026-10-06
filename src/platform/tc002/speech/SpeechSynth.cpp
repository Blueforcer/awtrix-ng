#include "platform/tc002/speech/SpeechSynth.h"

#include <algorithm>
#include <cmath>

#include "platform/tc002/speech/SpeechKernels.h"

namespace awtrix::speech {
namespace {

// Spectra the vocoder head computes at once.
constexpr std::size_t kHeadFrames = 4;

std::size_t radius(const BlockLayer& layer) { return layer.kernel / 2 * layer.dilation; }

std::size_t lookahead(const std::vector<BlockLayer>& blocks) {
  std::size_t sum = 0;
  for (const BlockLayer& block : blocks) sum += radius(block);
  return sum;
}

void denseRows(const DenseLayer& layer, const float* x, std::size_t xStride, std::size_t frames, float* y,
               std::size_t yStride, SpeechScratch& s) {
  for (std::size_t done = 0; done < frames;) {
    const std::size_t n = std::min(s.rows, frames - done);
    kernels::dense(layer, x + done * xStride, xStride, n, y + done * yStride, yStride, s.codes.data(), s.scales.data());
    done += n;
  }
}

// The next `count` outputs of a block over the frames in `in`, [count][width].
void block(const BlockLayer& layer, const FrameWindow& in, std::size_t count, float* out, SpeechScratch& s) {
  const std::size_t width = layer.width, kernel = layer.kernel, hidden = layer.expand.rows;
  const auto half = static_cast<std::ptrdiff_t>(kernel / 2);
  const auto dilation = static_cast<std::ptrdiff_t>(layer.dilation);
  for (std::size_t done = 0; done < count;) {
    const std::size_t n = std::min(s.rows, count - done);
    const auto first = static_cast<std::ptrdiff_t>(in.next() + done);
    for (std::size_t i = 0; i < n; ++i) {
      float* y = s.input.data() + i * width;
      std::copy_n(layer.depthwiseBiases, width, y);
      for (std::size_t j = 0; j < kernel; ++j) {
        const float* x = in.row(first + static_cast<std::ptrdiff_t>(i) + (static_cast<std::ptrdiff_t>(j) - half) * dilation);
        for (std::size_t c = 0; c < width; ++c) y[c] = y[c] + layer.depthwise[c * kernel + j] * x[c];
      }
    }
    kernels::layerNorm(layer.norm, s.input.data(), width, n, width);
    kernels::dense(layer.expand, s.input.data(), width, n, s.expanded.data(), hidden, s.codes.data(), s.scales.data());
    kernels::gelu(s.expanded.data(), n * hidden);
    float* y = out + done * width;
    kernels::dense(layer.project, s.expanded.data(), hidden, n, y, width, s.codes.data(), s.scales.data());
    for (std::size_t i = 0; i < n; ++i) {
      const float* x = in.row(first + static_cast<std::ptrdiff_t>(i));
      for (std::size_t c = 0; c < width; ++c) y[i * width + c] = x[c] + layer.gamma[c] * y[i * width + c];
    }
    done += n;
  }
}

// The next `count` outputs of a convolution `kernel` frames wide over `channels`, as the dense layer
// over input [c * kernel + j] = frame t + j - kernel / 2, channel c.
void convolve(const DenseLayer& layer, const FrameWindow& in, std::size_t count, std::size_t kernel,
              std::size_t channels, float* out, std::size_t outStride, SpeechScratch& s) {
  const auto half = static_cast<std::ptrdiff_t>(kernel / 2);
  const std::size_t width = channels * kernel;
  for (std::size_t done = 0; done < count;) {
    const std::size_t n = std::min(s.rows, count - done);
    const auto first = static_cast<std::ptrdiff_t>(in.next() + done);
    for (std::size_t i = 0; i < n; ++i) {
      float* gathered = s.input.data() + i * width;
      for (std::size_t j = 0; j < kernel; ++j) {
        const float* x = in.row(first + static_cast<std::ptrdiff_t>(i + j) - half);
        for (std::size_t c = 0; c < channels; ++c) gathered[c * kernel + j] = x[c];
      }
    }
    kernels::dense(layer, s.input.data(), width, n, out + done * outStride, outStride, s.codes.data(),
                   s.scales.data());
    done += n;
  }
}

}

SpeechScratch::SpeechScratch(const SpeechModel& model)
    : rows(kStepFrames + lookahead(model.decoder) + model.vocoderInKernel / 2 + lookahead(model.vocoder)) {
  const std::size_t d = model.encoderWidth(), p = model.duration.first.rows;
  width = std::max({d + 2, 3 * d, 3 * p, model.mels * model.vocoderInKernel});
  hidden = 0;
  stride = 0;
  for (const auto* blocks : {&model.encoder, &model.decoder, &model.vocoder}) {
    for (const BlockLayer& block : *blocks) {
      width = std::max(width, block.width);
      hidden = std::max(hidden, block.expand.rows);
      stride = std::max({stride, block.expand.stride, block.project.stride});
    }
  }
  for (const DenseLayer* layer : {&model.duration.first, &model.duration.second, &model.duration.out,
                                  &model.pitch.first, &model.pitch.second, &model.pitch.out, &model.pitchEmbedding,
                                  &model.frameIn, &model.melOut, &model.vocoderIn, &model.head})
    stride = std::max(stride, layer->stride);
  input.resize(rows * width);
  expanded.resize(rows * hidden);
  codes.resize(rows * stride);
  scales.resize(rows);
}

void FrameWindow::reserve(std::size_t rows, std::size_t width) { rows_.assign(rows * width, 0.0f); }

void FrameWindow::begin(std::size_t frames, std::size_t width, std::size_t radius) {
  frames_ = frames;
  width_ = width;
  radius_ = radius;
  received_ = 0;
  next_ = 0;
  first_ = -static_cast<std::ptrdiff_t>(radius);
  held_ = radius;
  std::fill_n(rows_.begin(), radius * width, 0.0f);
}

float* FrameWindow::append(std::size_t count) {
  float* at = rows_.data() + held_ * width_;
  if (!count) return at;
  held_ += count;
  received_ += count;
  if (received_ == frames_) {
    std::fill_n(rows_.begin() + static_cast<std::ptrdiff_t>(held_ * width_), radius_ * width_, 0.0f);
    held_ += radius_;
  }
  return at;
}

std::size_t FrameWindow::ready() const {
  const std::ptrdiff_t end = first_ + static_cast<std::ptrdiff_t>(held_) - static_cast<std::ptrdiff_t>(radius_);
  const auto next = static_cast<std::ptrdiff_t>(next_);
  return end > next ? static_cast<std::size_t>(end - next) : 0;
}

void FrameWindow::consume(std::size_t count) {
  next_ += count;
  const std::ptrdiff_t keep = static_cast<std::ptrdiff_t>(next_) - static_cast<std::ptrdiff_t>(radius_);
  if (keep <= first_) return;
  const auto drop = static_cast<std::size_t>(keep - first_);
  std::copy(rows_.begin() + static_cast<std::ptrdiff_t>(drop * width_),
            rows_.begin() + static_cast<std::ptrdiff_t>(held_ * width_), rows_.begin());
  first_ = keep;
  held_ -= drop;
}

AcousticStream::AcousticStream(const SpeechModel& model, SpeechScratch& scratch)
    : model_(model), scratch_(scratch), lookahead_(lookahead(model.decoder)) {
  const std::size_t tokens = kMaxChunkPhones + 1, d = model.encoderWidth(), e = model.decoderWidth();
  std::size_t phoneRadius = 1;
  for (const BlockLayer& block : model.encoder) phoneRadius = std::max(phoneRadius, radius(block));
  const std::size_t phoneWidth = std::max(d, model.duration.first.rows);
  phonesA_.reserve(tokens + 2 * phoneRadius, phoneWidth);
  phonesB_.reserve(tokens + 2 * phoneRadius, phoneWidth);
  encoded_.resize(tokens * d);
  logDurations_.resize(tokens);
  pitch_.resize(tokens);
  durations_.resize(tokens);
  windows_.resize(model.decoder.size());
  std::size_t input = kStepFrames;
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    const std::size_t r = radius(model.decoder[i]);
    windows_[i].reserve(input + 3 * r, e);
    input += r;
  }
  last_.resize(input * e);
}

void AcousticStream::predict(const PredictorLayers& layers, float* out) {
  const std::size_t d = model_.encoderWidth(), p = layers.first.rows;
  phonesA_.begin(tokens_, d, 1);
  std::copy_n(encoded_.begin(), tokens_ * d, phonesA_.append(tokens_));
  phonesB_.begin(tokens_, p, 1);
  float* first = phonesB_.append(tokens_);
  convolve(layers.first, phonesA_, tokens_, 3, d, first, p, scratch_);
  kernels::relu(first, tokens_ * p);
  kernels::layerNorm(layers.firstNorm, first, p, tokens_, p);
  phonesA_.begin(tokens_, p, 0);
  float* second = phonesA_.append(tokens_);
  convolve(layers.second, phonesB_, tokens_, 3, p, second, p, scratch_);
  kernels::relu(second, tokens_ * p);
  kernels::layerNorm(layers.secondNorm, second, p, tokens_, p);
  denseRows(layers.out, second, p, tokens_, out, 1, scratch_);
}

void AcousticStream::begin(const Token* tokens, std::size_t count) {
  const SpeechModel& m = model_;
  const std::size_t d = m.encoderWidth(), e = m.decoderWidth();
  tokens_ = count;
  FrameWindow* in = &phonesA_;
  FrameWindow* out = &phonesB_;
  in->begin(count, d, radius(m.encoder[0]));
  float* x = in->append(count);
  for (std::size_t t = 0; t < count; ++t) {
    const Token& token = tokens[t];
    const float* phone = m.phones + token.phone * d;
    const float* stress = m.stresses + token.stress * d;
    const float* wordEnd = m.wordEnds + token.wordEnd * d;
    const float* punctuation = m.punctuations + token.punctuation * d;
    for (std::size_t c = 0; c < d; ++c) x[t * d + c] = phone[c] + stress[c] + wordEnd[c] + punctuation[c];
  }
  for (std::size_t i = 0; i < m.encoder.size(); ++i) {
    float* y = encoded_.data();
    if (i + 1 < m.encoder.size()) {
      out->begin(count, d, radius(m.encoder[i + 1]));
      y = out->append(count);
    }
    block(m.encoder[i], *in, count, y, scratch_);
    std::swap(in, out);
  }
  kernels::layerNorm(m.encoderNorm, encoded_.data(), d, count, d);
  predict(m.duration, logDurations_.data());
  predict(m.pitch, pitch_.data());
  frames_ = 0;
  for (std::size_t t = 0; t < count; ++t) {
    const float frames = std::floor((std::exp(logDurations_[t]) - 1.0f) + 0.5f);
    // A NaN prediction counts as one frame too.
    const float clamped = frames >= 1.0f ? std::min(frames, static_cast<float>(m.maxDuration)) : 1.0f;
    durations_[t] = static_cast<uint8_t>(clamped);
    frames_ += durations_[t];
  }
  phonesA_.begin(count, 1, 1);
  std::copy_n(pitch_.begin(), count, phonesA_.append(count));
  phonesB_.begin(count, d, 0);
  float* embedded = phonesB_.append(count);
  convolve(m.pitchEmbedding, phonesA_, count, 3, 1, embedded, d, scratch_);
  for (std::size_t i = 0; i < count * d; ++i) encoded_[i] = encoded_[i] + embedded[i];
  phone_ = repeat_ = emitted_ = 0;
  for (std::size_t i = 0; i < windows_.size(); ++i) windows_[i].begin(frames_, e, radius(m.decoder[i]));
}

std::size_t AcousticStream::next(float* mel) {
  const SpeechModel& m = model_;
  const std::size_t d = m.encoderWidth(), e = m.decoderWidth(), features = d + 2;
  const std::size_t regulated = std::min(kStepFrames, frames_ - emitted_);
  float* rows = scratch_.input.data();
  for (std::size_t i = 0; i < regulated; ++i) {
    float* row = rows + i * features;
    const auto duration = static_cast<float>(durations_[phone_]);
    std::copy_n(encoded_.begin() + static_cast<std::ptrdiff_t>(phone_ * d), d, row);
    row[d] = (static_cast<float>(repeat_) + 0.5f) / duration;
    row[d + 1] = static_cast<float>(std::log1p(static_cast<double>(duration))) / 4.0f;
    if (++repeat_ == durations_[phone_]) {
      ++phone_;
      repeat_ = 0;
    }
  }
  emitted_ += regulated;
  denseRows(m.frameIn, rows, features, regulated, windows_[0].append(regulated), e, scratch_);
  std::size_t count = 0;
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    FrameWindow& in = windows_[i];
    count = in.ready();
    block(m.decoder[i], in, count, i + 1 < windows_.size() ? windows_[i + 1].append(count) : last_.data(), scratch_);
    in.consume(count);
  }
  kernels::layerNorm(m.decoderNorm, last_.data(), e, count, e);
  denseRows(m.melOut, last_.data(), e, count, mel, m.mels, scratch_);
  return count;
}

VocoderStream::VocoderStream(const SpeechModel& model, SpeechScratch& scratch, std::size_t maxInput)
    : model_(model), scratch_(scratch), wave_(model.fftSize, model.hop) {
  const std::size_t inputRadius = model.vocoderInKernel / 2, v = model.vocoderWidth();
  input_.reserve(maxInput + 3 * inputRadius, model.mels);
  std::size_t frames = maxInput + inputRadius;
  windows_.resize(model.vocoder.size());
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    const std::size_t r = radius(model.vocoder[i]);
    windows_[i].reserve(frames + 3 * r, v);
    frames += r;
  }
  last_.resize(frames * v);
  spectra_.resize(kHeadFrames * model.head.rows);
  maxSamples_ = frames * wave_.maxSamples();
}

void VocoderStream::begin(std::size_t frames) {
  input_.begin(frames, model_.mels, model_.vocoderInKernel / 2);
  for (std::size_t i = 0; i < windows_.size(); ++i)
    windows_[i].begin(frames, model_.vocoderWidth(), radius(model_.vocoder[i]));
  wave_.begin(frames);
}

std::size_t VocoderStream::push(const float* mel, std::size_t count, int16_t* out) {
  const SpeechModel& m = model_;
  const std::size_t v = m.vocoderWidth(), spectrum = m.head.rows;
  std::copy_n(mel, count * m.mels, input_.append(count));
  std::size_t ready = input_.ready();
  float* x = windows_[0].append(ready);
  convolve(m.vocoderIn, input_, ready, m.vocoderInKernel, m.mels, x, v, scratch_);
  kernels::layerNorm(m.vocoderInNorm, x, v, ready, v);
  input_.consume(ready);
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    FrameWindow& in = windows_[i];
    ready = in.ready();
    block(m.vocoder[i], in, ready, i + 1 < windows_.size() ? windows_[i + 1].append(ready) : last_.data(), scratch_);
    in.consume(ready);
  }
  kernels::layerNorm(m.vocoderNorm, last_.data(), v, ready, v);
  std::size_t written = 0;
  for (std::size_t f = 0; f < ready; f += kHeadFrames) {
    const std::size_t n = std::min(kHeadFrames, ready - f);
    denseRows(m.head, last_.data() + f * v, v, n, spectra_.data(), spectrum, scratch_);
    for (std::size_t k = 0; k < n; ++k) written += wave_.push(spectra_.data() + k * spectrum, out + written);
  }
  return written;
}

}
