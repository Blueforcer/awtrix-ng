#pragma once

#include <thread>
#include <utility>

namespace awtrix::posix {

template <class Body>
bool startThread(std::thread& thread, Body&& body) {
  try {
    thread = std::thread(std::forward<Body>(body));
    return true;
  } catch (...) {
    return false;
  }
}

}
