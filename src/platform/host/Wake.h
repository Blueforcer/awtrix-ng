#pragma once

#include "platform/host/Socket.h"

namespace awtrix::os {

class Wake {
 public:
  Wake();
  ~Wake();
  Wake(const Wake&) = delete;
  Wake& operator=(const Wake&) = delete;

  bool valid() const { return read_ != kNoSocket; }
  Socket handle() const { return read_; }
  void signal();
  void drain();

 private:
  Socket read_ = kNoSocket;
  Socket write_ = kNoSocket;
};

}
