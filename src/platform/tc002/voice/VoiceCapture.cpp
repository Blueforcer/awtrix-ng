#include "platform/tc002/voice/VoiceCapture.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "platform/tc002/wake/ModelFiles.h"

namespace awtrix::tc002::voice {
namespace {

static_assert(kMicrophonePcmRate == wake::WakeFrontend::kSampleRate,
              "the endpointer takes the microphone's PCM as it comes");
constexpr uint32_t kSamplesPerMs = kMicrophonePcmRate / 1000;

uint8_t loudness(const int16_t* samples, std::size_t count) {
  if (count == 0) return 0;
  double sum = 0.0;
  for (std::size_t i = 0; i < count; ++i) sum += static_cast<double>(samples[i]) * samples[i];
  const double rms = std::sqrt(sum / static_cast<double>(count)) / 32768.0;
  if (rms <= 0.001) return 0;
  const double share = std::min(1.0, (20.0 * std::log10(rms) + 60.0) / 60.0);
  return static_cast<uint8_t>(std::lround(share * 255.0));
}

}

VoiceCapture::VoiceCapture(MicrophonePort microphone, std::function<void(MicrophoneMark)> startFrom,
                           std::string modelPath)
    : microphone_(std::move(microphone)), startFrom_(std::move(startFrom)), modelPath_(std::move(modelPath)) {}

VoiceCapture::~VoiceCapture() { cancel(); }

bool VoiceCapture::prepare(std::string& error) {
  if (endpointer_) return true;
  if (modelPath_.empty()) {
    error = "no voice activity model";
    return false;
  }
  wake::ModelFiles files;
  if (!wake::readModelFiles(modelPath_, files, error)) return false;
  wake::Endpointer& endpointer = endpointer_.emplace();
  if (!endpointer.load(files.model, error)) {
    endpointer_.reset();
    return false;
  }
  if (!endpointer.configure(static_cast<float>(files.cutoff), files.window, error)) {
    error = files.settingsPath + ": " + error;
    endpointer_.reset();
    return false;
  }
  return true;
}

bool VoiceCapture::start(const std::string& path, const wake::EndpointSettings& settings, MicrophoneMark from,
                         int64_t now) {
  std::string error;
  if (active() || !prepare(error)) return false;
  if (!wav_.open(path, kMicrophonePcmRate)) {
    endpointer_.reset();
    return false;
  }
  result_.reset();
  maxMs_ = settings.maxMs;
  endpointer_->start(settings);
  startFrom_(from);
  level_ = 0;
  microphone_.open(now);
  return true;
}

std::size_t VoiceCapture::maxBytes(const wake::EndpointSettings& settings) const {
  const std::size_t stepMs = endpointer_ ? static_cast<std::size_t>(endpointer_->stepMs()) : 0;
  return wavBytes((std::size_t{settings.maxMs} + stepMs) * kSamplesPerMs);
}

void VoiceCapture::receive(const StreamEvent& event, int64_t now) {
  switch (microphone_.receive(event, now)) {
    case MicrophoneClient::Report::None:
    case MicrophoneClient::Report::Stopped:
      return;
    case MicrophoneClient::Report::Unavailable:
      return finish(Outcome::Failed, microphone_.error());
    case MicrophoneClient::Report::Started:
      recordingAt_ = now;
      return;
    case MicrophoneClient::Report::Gap:
      return finish(Outcome::Failed, "the microphone stream skipped audio");
    case MicrophoneClient::Report::Audio:
      return record(event.samples);
    case MicrophoneClient::Report::Ended:
      return finish(Outcome::Failed,
                    microphone_.error().empty() ? "the microphone stream ended" : microphone_.error());
  }
}

void VoiceCapture::record(const std::vector<int16_t>& block) {
  level_ = loudness(block.data(), block.size());
  const int16_t* const first = block.data();
  const int16_t* samples = first;
  std::size_t count = block.size();
  for (wake::Endpoint kind; (kind = endpointer_->next(samples, count)) != wake::Endpoint::None;) {
    if (kind == wake::Endpoint::Started) continue;
    if (!wav_.write(first, static_cast<std::size_t>(samples - first)))
      return finish(Outcome::Failed, "could not write the recording");
    return finish(kind == wake::Endpoint::Silence ? Outcome::Silence : Outcome::Recorded);
  }
  if (!wav_.write(first, block.size())) finish(Outcome::Failed, "could not write the recording");
}

void VoiceCapture::tick(int64_t now) {
  if (recording() && now - recordingAt_ >= maxMs_ + kStreamAnswerMs)
    return finish(Outcome::Failed, "the microphone stream stalled");
  if (microphone_.tick(now) == MicrophoneClient::Report::Ended) finish(Outcome::Failed, microphone_.error());
}

void VoiceCapture::stop() {
  if (active()) finish(wav_.samples() ? Outcome::Recorded : Outcome::Silence);
}

void VoiceCapture::cancel() {
  result_.reset();
  endpointer_.reset();
  if (!active()) return;
  microphone_.cancel();
  wav_.close();
  level_ = 0;
}

void VoiceCapture::finish(Outcome outcome, std::string error) {
  microphone_.cancel();
  endpointer_.reset();
  const uint32_t samples = wav_.samples();
  if (!wav_.close() && outcome == Outcome::Recorded) {
    outcome = Outcome::Failed;
    error = "could not write the recording";
  }
  result_ = Result{outcome, samples / kSamplesPerMs, wavBytes(samples), std::move(error)};
  level_ = 0;
}

std::optional<VoiceCapture::Result> VoiceCapture::take() {
  std::optional<Result> out;
  out.swap(result_);
  return out;
}

}
