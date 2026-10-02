#include "platform/rp2040/UnicornToneSink.h"
#include <LittleFS.h>
#include <hardware/dma.h>
#include <hardware/pio.h>

namespace awtrix {
namespace {
constexpr int AmpEnable = 22; // LOW shuts the amplifier down; HIGH enables it.
uint32_t dmaClaims() {
  uint32_t mask = 0;
  for (uint i = 0; i < NUM_DMA_CHANNELS; ++i)
    if (dma_channel_is_claimed(i)) mask |= 1u << i;
  return mask;
}
uint32_t smClaims() {
  uint32_t mask = 0;
  for (uint p = 0; p < NUM_PIOS; ++p)
    for (uint s = 0; s < NUM_PIO_STATE_MACHINES; ++s)
      if (pio_sm_is_claimed(pio_get_instance(p), s)) mask |= 1u << (p * 4 + s);
  return mask;
}
}

bool UnicornToneSink::open() {
  if (running_) return true;
  const auto beforeSm = smClaims(), beforeDma = dmaClaims();
  running_ = i2s_.begin(sound::ToneSamples::SampleRate);
  Serial.printf("tone: %s; new PIO-SM mask 0x%lx, DMA mask 0x%lx\n",
                running_ ? "ready" : "unavailable", (unsigned long)(smClaims() & ~beforeSm),
                (unsigned long)(dmaClaims() & ~beforeDma));
  return running_;
}

void UnicornToneSink::begin() {
  pinMode(AmpEnable, OUTPUT);
  digitalWrite(AmpEnable, LOW);
  i2s_.setBCLK(10); // LRCLK is BCLK + 1.
  i2s_.setDATA(9);
  i2s_.setBitsPerSample(16);
  i2s_.setBuffers(BufferCount, BufferWords, 0);
  setVolume(volume_);
  // Called after the display and CYW43. Arduino-pico uses the same SDK
  // program/SM/DMA claim bitmaps, never fixed state machines or DMA channels.
  ready_ = open();
}

void UnicornToneSink::setVolume(uint8_t percent) {
  volume_ = percent > 100 ? 100 : percent;
  samples_.setVolume(volume_);
  digitalWrite(AmpEnable, playing_ && volume_ ? HIGH : LOW);
}

bool UnicornToneSink::playMelodyFile(const std::string& name) {
  if (!ready_ || !rtttl::validName(name)) return false;
  File file = LittleFS.open((std::string("/MELODIES/") + name + ".txt").c_str(), "r");
  if (!file || file.size() > rtttl::kMaxLength) return false;
  std::string text;
  text.reserve(file.size());
  while (file.available()) text.push_back(static_cast<char>(file.read()));
  return playRtttl(text);
}

bool UnicornToneSink::playRtttl(const std::string& text) {
  if (!ready_ || !samples_.play(text)) return false;
  // Discard old queued samples on replacement, with the amplifier muted.
  digitalWrite(AmpEnable, LOW);
  if (running_) i2s_.end();
  running_ = false;
  playing_ = false;
  if (!open()) { samples_.stop(); return false; }
  partial_ = 0;
  draining_ = false;
  playing_ = true;
  tick(); // Prime without waiting for DMA.
  digitalWrite(AmpEnable, volume_ ? HIGH : LOW);
  return true;
}

void UnicornToneSink::stop() {
  digitalWrite(AmpEnable, LOW);
  samples_.stop();
  playing_ = draining_ = false;
  partial_ = 0;
  if (running_) i2s_.end(); // Abort/release DMA; never flush() (which busy-waits).
  running_ = false;
}

void UnicornToneSink::tick() {
  if (!playing_ || !running_) return;
  // A strict finite budget, called on every loop, before the frame-rate early
  // return. Every write is explicitly non-blocking; underrun produces silence.
  for (int n = 0; n < BufferCount * BufferWords; ++n) {
    if (!samples_.playing() && partial_ == 0) break;
    if (i2s_.availableForWrite() < 4) break;
    const auto word = sound::ToneSamples::stereo(samples_.next());
    i2s_.write(static_cast<int32_t>(word), false);
    partial_ = (partial_ + 1) % BufferWords; // Pad the last partial DMA buffer with zeros.
  }
  if (samples_.playing() || partial_ != 0) return;
  // Wait cooperatively for every buffer to return, then allow the PIO FIFO
  // (< 1 ms) to empty before asserting mute. No wall-clock melody truncation.
  if (i2s_.availableForWrite() == BufferCount * BufferWords * 4) {
    if (!draining_) { draining_ = true; drainedAt_ = millis(); }
    else if (millis() - drainedAt_ >= 1) stop();
  }
}
}
