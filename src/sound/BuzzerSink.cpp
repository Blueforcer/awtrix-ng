#include "sound/BuzzerSink.h"

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

void BuzzerSink::begin() {
  if (pin_ < 0) return;
  // LEDC channel 0, and LOW as the idle level because the buzzer on these boards is active high.
  player_ = new MelodyPlayer(static_cast<unsigned char>(pin_), 0, LOW);
  setVolume(volume_);
}

void BuzzerSink::setVolume(uint8_t percent) {
  volume_ = percent > 100 ? 100 : percent;
  if (player_) player_->setVolume(map(volume_, 0, 100, 0, 255));
}

bool BuzzerSink::playMelodyFile(const std::string& name) {
  if (!player_) return false;
  // A melody "file" is an RTTTL one-liner in /MELODIES/<name>.txt, the same syntax playRtttl takes.
  const String path = String("/MELODIES/") + name.c_str() + ".txt";
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  std::string content;
  content.reserve(f.size());
  while (f.available()) content.push_back(static_cast<char>(f.read()));
  f.close();

  return playRtttl(content);
}

bool BuzzerSink::playRtttl(const std::string& melody) {
  if (!player_) return false;
  const rtttl::Parse p = rtttl::parse(melody);
  if (!p.ok) return false;
  Melody m = toMelody(p);
  player_->playAsync(m);
  return true;
}

void BuzzerSink::stop() {
  if (player_) player_->stop();
}

bool BuzzerSink::isPlaying() const { return player_ && player_->isPlaying(); }

}
