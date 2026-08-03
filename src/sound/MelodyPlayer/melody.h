#ifndef MELODY_H
#define MELODY_H

#include <Arduino.h>

#include <memory>
#include <vector>

struct NoteDuration {
  unsigned short frequency;
  unsigned short duration;
};

class Melody {
public:
  Melody() : notes(nullptr){};

  Melody(String title, unsigned short timeUnit, std::shared_ptr<std::vector<NoteDuration>> notes, bool automaticSilence)
    : title(title), timeUnit(timeUnit), notes(notes), automaticSilence(automaticSilence){};

  String getTitle() const {
    return title;
  };

  unsigned short getTimeUnit() const {
    return timeUnit;
  };

  unsigned short getLength() const {
    if (notes == nullptr) return 0;
    return (*notes).size();
  }

  NoteDuration getNote(unsigned short i) const {
    if (i < getLength()) { return (*notes)[i]; }
    return { 0, 0 };
  }

  bool getAutomaticSilence() const {
    return automaticSilence;
  }

  bool isValid() const {
    return notes != nullptr && (*notes).size() != 0;
  }

  explicit operator bool() const {
    return isValid();
  }

private:
  String title;
  unsigned short timeUnit;
  std::shared_ptr<std::vector<NoteDuration>> notes;
  const static unsigned short maxLength = 1000;
  bool automaticSilence;

  const static bool debug = false;
};

#endif
