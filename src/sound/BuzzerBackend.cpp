#include "sound/BuzzerBackend.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <memory>
#include <vector>

#include "core/sound/Rtttl.h"
#include "sound/MelodyPlayer/melody_player.h"

namespace awtrix {

namespace {

// The notes are shared, not copied: playAsync() keeps playing from them long after the local
// Melody in the caller has gone out of scope.
Melody toMelody(const rtttl::Parse& p) {
  auto notes = std::make_shared<std::vector<NoteDuration>>();
  notes->reserve(p.notes.size());
  for (const rtttl::Note& n : p.notes)
    notes->push_back({n.frequency, n.duration});
  return Melody(String(p.title.c_str()), p.timeUnit, notes, false);
}

}

void BuzzerBackend::begin() {
  if (pin_ < 0) return;
  // LEDC channel 0, and LOW as the idle level because the buzzer on these boards is active high.
  player_ = new MelodyPlayer(static_cast<unsigned char>(pin_), 0, LOW);
  setVolume(volume_);
}

void BuzzerBackend::setVolume(uint8_t volume) {
  volume_ = volume;
  // 0-30 is the DFPlayer scale, which the whole firmware uses; the buzzer wants a PWM duty.
  if (player_) player_->setVolume(map(volume, 0, 30, 0, 255));
}

bool BuzzerBackend::playFile(const std::string& id) {
  if (!player_) return false;
  // Melody "files" are RTTTL one-liners in /MELODIES/<id>.txt, the same syntax playRtttl takes.
  const String path = String("/MELODIES/") + id.c_str() + ".txt";
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  std::string content;
  content.reserve(f.size());
  while (f.available()) content.push_back(static_cast<char>(f.read()));
  f.close();

  const rtttl::Parse p = rtttl::parse(content);
  if (!p.ok) return false;
  Melody m = toMelody(p);
  player_->playAsync(m);
  return true;
}

void BuzzerBackend::playRtttl(const std::string& melody) {
  if (!player_) return;
  const rtttl::Parse p = rtttl::parse(melody);
  if (!p.ok) return;
  Melody m = toMelody(p);
  player_->playAsync(m);
}

void BuzzerBackend::stop() {
  if (player_) player_->stop();
}

bool BuzzerBackend::isPlaying() const { return player_ && player_->isPlaying(); }

}
