#include "melody_player.h"
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&...args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// Silence clipped off the end of each note so that two identical notes in a row are heard as
// two, not as one long one. Only applied when the note is long enough to spare it.
static const unsigned int noteGapMs = 6;

#ifdef ESP32
static const unsigned char toneResolutionBits = 10;

// Volume becomes the PWM duty cycle. A square wave is loudest at 50%, so that is the top of the
// range — going higher only narrows the pulse again.
static uint32_t dutyFor(byte volume)
{
  const uint32_t loudest = (1u << toneResolutionBits) / 2;
  return (uint32_t)volume * loudest / 255u;
}
#endif

void MelodyPlayer::play()
{
  if (melodyState == nullptr)
  {
    return;
  }

  turnOn();
  state = State::PLAY;

  melodyState->advance();
  while (melodyState->getIndex() + melodyState->isSilence() < melodyState->melody.getLength())
  {
    NoteDuration computedNote = melodyState->getCurrentComputedNote();
    if (debug)
      Serial.println(String("Playing: frequency:") + computedNote.frequency + " duration:" + computedNote.duration);
    if (melodyState->isSilence())
    {
#ifdef ESP32
      ledcWriteTone(pwmChannel, 0);
#else
      noTone(pin);
#endif
      delay(0.3f * computedNote.duration);
    }
    else
    {
#ifdef ESP32
      ledcWriteTone(pwmChannel, computedNote.frequency);
      if (computedNote.frequency)
        ledcWrite(pwmChannel, dutyFor(volume));
#else
      tone(pin, computedNote.frequency);
#endif
      const unsigned int gap =
          computedNote.frequency && computedNote.duration > 2 * noteGapMs ? noteGapMs : 0;
      delay(computedNote.duration - gap);
      if (gap)
      {
#ifdef ESP32
        ledcWrite(pwmChannel, 0);
#else
        noTone(pin);
#endif
        delay(gap);
      }
    }
    melodyState->advance();
  }
  stop();
}

void MelodyPlayer::play(Melody &melody)
{
  if (!melody)
  {
    return;
  }
  // Same hazard as playAsync(melody, ...): drop a pending ticker callback before the swap.
  haltPlay();
  melodyState = make_unique<MelodyState>(melody);
  play();
}

void changeTone(MelodyPlayer *player);

void releaseTone(MelodyPlayer *player)
{
  player->sounding = false;
#ifdef ESP32
  ledcWrite(player->pwmChannel, 0);
  player->ticker.once_ms((float)noteGapMs, changeTone, player);
#else
  noTone(player->pin);
  player->ticker.once_ms_scheduled((float)noteGapMs, std::bind(changeTone, player));
#endif
}

void changeTone(MelodyPlayer *player)
{
  // The ticker callback can fire on the timer task while the main thread is in stop() or is
  // swapping melodyState for a new melody — never dereference it unchecked.
  if (player->melodyState == nullptr)
  {
    return;
  }
  player->melodyState->advance();
  if (player->melodyState->getIndex() + player->melodyState->isSilence() < player->melodyState->melody.getLength())
  {
    NoteDuration computedNote(player->melodyState->getCurrentComputedNote());

    float duration = player->melodyState->getRemainingNoteDuration();
    if (duration > 0)
    {
      player->melodyState->resetRemainingNoteDuration();
    }
    else
    {
      if (player->melodyState->isSilence())
      {
        duration = 0.3f * computedNote.duration;
      }
      else
      {
        duration = computedNote.duration;
      }
    }
    if (player->debug)
      Serial.println(String("Playing async: freq=") + computedNote.frequency + " dur=" + duration + " iteration=" + player->melodyState->getIndex());

    if (player->melodyState->isSilence())
    {
      player->sounding = false;
      if (!player->muted)
      {
#ifdef ESP32
        ledcWriteTone(player->pwmChannel, 0);
#else
        tone(player->pin, 0);
#endif
      }

#ifdef ESP32
      player->ticker.once_ms(duration, changeTone, player);
#else
      player->ticker.once_ms_scheduled(duration, std::bind(changeTone, player));
#endif
    }
    else
    {
      player->sounding = computedNote.frequency != 0 && !player->muted;
      if (!player->muted)
      {
#ifdef ESP32
        ledcWriteTone(player->pwmChannel, computedNote.frequency);
        if (computedNote.frequency)
          ledcWrite(player->pwmChannel, dutyFor(player->volume));
#else
        tone(player->pin, computedNote.frequency);
#endif
      }

      const bool separate = computedNote.frequency && duration > 2 * noteGapMs;
#ifdef ESP32
      if (separate)
        player->ticker.once_ms(duration - noteGapMs, releaseTone, player);
      else
        player->ticker.once_ms(duration, changeTone, player);
#else
      if (separate)
        player->ticker.once_ms_scheduled(duration - noteGapMs, std::bind(releaseTone, player));
      else
        player->ticker.once_ms_scheduled(duration, std::bind(changeTone, player));
#endif
    }
    player->supportSemiNote = millis() + duration;
  }
  else
  {
    player->stop();
    if (player->loop)
    {
      player->playAsync();
    }
    else if (player->stopCallback != NULL)
    {
      player->stopCallback();
    }
  }
}

void MelodyPlayer::playAsync()
{
  if (melodyState == nullptr)
  {
    return;
  }

  turnOn();
  state = State::PLAY;

#ifdef ESP32
  ticker.once(0, changeTone, this);
#else
  ticker.once_scheduled(0, std::bind(changeTone, this));
#endif
}

void MelodyPlayer::playAsync(Melody &melody, bool loopMelody, void (*callback)(void))
{
  if (!melody)
  {
    return;
  }
  // Detach any pending ticker callback before freeing the old state, otherwise changeTone can
  // run against a melodyState that no longer exists.
  haltPlay();
  melodyState = make_unique<MelodyState>(melody);
  loop = loopMelody;
  stopCallback = callback;
  playAsync();
}

void MelodyPlayer::stop()
{
  if (melodyState == nullptr)
  {
    return;
  }

  haltPlay();
  state = State::STOP;
  melodyState->reset();
}

void MelodyPlayer::pause()
{
  if (melodyState == nullptr)
  {
    return;
  }

  haltPlay();
  state = State::PAUSE;
  melodyState->saveRemainingNoteDuration(supportSemiNote);
}

void MelodyPlayer::transferMelodyTo(MelodyPlayer &destPlayer)
{
  if (melodyState == nullptr)
  {
    return;
  }

  destPlayer.stop();

  bool playing = isPlaying();

  haltPlay();
  state = State::STOP;
  melodyState->saveRemainingNoteDuration(supportSemiNote);
  destPlayer.melodyState = std::move(melodyState);

  if (playing)
  {
    destPlayer.playAsync();
  }
  else
  {
    destPlayer.state = state;
  }
}

void MelodyPlayer::duplicateMelodyTo(MelodyPlayer &destPlayer)
{
  if (melodyState == nullptr)
  {
    return;
  }

  destPlayer.stop();
  destPlayer.melodyState = make_unique<MelodyState>(*(this->melodyState));
  destPlayer.melodyState->saveRemainingNoteDuration(supportSemiNote);

  if (isPlaying())
  {
    destPlayer.playAsync();
  }
  else
  {
    destPlayer.state = state;
  }
}

#ifdef ESP32
MelodyPlayer::MelodyPlayer(unsigned char pin, unsigned char pwmChannel, bool offLevel)
    : pin(pin), pwmChannel(pwmChannel), offLevel(offLevel), state(State::STOP), melodyState(nullptr)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, offLevel);
};
#else
MelodyPlayer::MelodyPlayer(unsigned char pin, bool offLevel)
    : pin(pin), offLevel(offLevel), state(State::STOP), melodyState(nullptr)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, offLevel);
};
#endif

void MelodyPlayer::haltPlay()
{
  ticker.detach();
  turnOff();
}

void MelodyPlayer::turnOn()
{
#ifdef ESP32
  const int resolution = 8;
  ledcSetup(pwmChannel, 2000, resolution);
  ledcAttachPin(pin, pwmChannel);
  ledcWrite(pwmChannel, 0);
#endif
}

void MelodyPlayer::setVolume(byte newVolume)
{
  volume = newVolume;
#ifdef ESP32
  if (state == State::PLAY && sounding)
  {
    ledcWrite(pwmChannel, dutyFor(volume));
  }
#endif
}

void MelodyPlayer::turnOff()
{
  sounding = false;
#ifdef ESP32
  ledcWrite(pwmChannel, 0);
  ledcDetachPin(pin);
#else
  noTone(pin);
#endif

  pinMode(pin, OUTPUT);
  digitalWrite(pin, offLevel);
}

void MelodyPlayer::mute()
{
  muted = true;
  sounding = false;
}

void MelodyPlayer::unmute()
{
#ifdef ESP32
  ledcAttachPin(pin, pwmChannel);
#endif
  muted = false;
}

void MelodyPlayer::changeTempo(int newTempo)
{
  if (melodyState == nullptr)
  {
    return;
  }
  melodyState->changeTempo(newTempo);
}