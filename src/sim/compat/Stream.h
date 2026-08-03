#pragma once


#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;

  void setTimeout(unsigned long timeout) { _timeout = timeout; }

 protected:
  unsigned long _timeout = 1000;
};
