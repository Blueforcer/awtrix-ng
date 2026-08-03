#ifndef MELODY_PLAYER_H
#define MELODY_PLAYER_H

#include "melody.h"
#include <Ticker.h>
#include <memory>

class MelodyPlayer {
public:
#ifdef ESP32
  MelodyPlayer(unsigned char pin, unsigned char pwmChannel = 0, bool offLevel = HIGH);
#else
  MelodyPlayer(unsigned char pin, bool offLevel = HIGH);
#endif

  void play();

  void play(Melody& melody);

  void playAsync();

  void playAsync(Melody& melody, bool loop = false, void(*stopCallback)(void) = NULL);

  void stop();

  void pause();

  void mute();

  void unmute();

  bool isPlaying() const {
    return state == State::PLAY;
  }

  void changeTempo(int newTempo);

  void setVolume(byte volume);

  void transferMelodyTo(MelodyPlayer& destination);

  void duplicateMelodyTo(MelodyPlayer& destination);

private:
  unsigned char pin;
  byte volume = 128;
  bool loop = false;
  bool muted = false;
  bool sounding = false;
  void (*stopCallback)(void) = NULL;

#ifdef ESP32
  unsigned char pwmChannel;
#endif

  bool offLevel;

  class MelodyState {
  public:
    MelodyState() : first(true), index(0), remainingNoteTime(0), timeUnit(0) {};
    MelodyState(const Melody& melody)
      : melody(melody), first(true), silence(false), index(0), remainingNoteTime(0), timeUnit(melody.getTimeUnit()){};
    Melody melody;

    unsigned short getIndex() const {
      return index;
    }

    bool isSilence() const {
      return silence;
    }

    void changeTempo(int newTempo) {
      timeUnit = (60 * 1000 * 4 / newTempo / 32);
    }

    void advance() {
      if (first) {
        first = false;
        return;
      }
      if (remainingNoteTime != 0) { return; }

      if (melody.getAutomaticSilence()) {
        if (silence) {
          index++;
          silence = false;
        } else {
          silence = true;
        }
      } else {
        index++;
      }
    }

    void reset() {
      first = true;
      index = 0;
      remainingNoteTime = 0;
      silence = false;
    }

    void saveRemainingNoteDuration(unsigned long supportSemiNote) {
      remainingNoteTime = supportSemiNote - millis();
      if (remainingNoteTime < 10) { remainingNoteTime = 0; }
    }

    void resetRemainingNoteDuration() {
      remainingNoteTime = 0;
    }

    unsigned short getRemainingNoteDuration() const {
      return remainingNoteTime;
    }

    NoteDuration getCurrentComputedNote() const {
      NoteDuration note = melody.getNote(getIndex());
      note.duration = timeUnit * note.duration;
      note.duration /= 2;
      return note;
    }

  private:
    bool first;
    bool silence;
    unsigned short index;
    unsigned short timeUnit;

    unsigned short remainingNoteTime;
  };

  enum class State { STOP, PLAY, PAUSE };

  State state;

  std::unique_ptr<MelodyState> melodyState;

  unsigned long supportSemiNote;

  Ticker ticker;

  const static bool debug = false;

  friend void changeTone(MelodyPlayer* melody);

  friend void releaseTone(MelodyPlayer* melody);

  void haltPlay();

  void turnOn();

  void turnOff();
};

#endif