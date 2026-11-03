#include "../support.h"
// The cover lookup's thread: none until a lookup asks for one. The lookup itself is the iPhone test's.
#include <cstdio>

#include "platform/linux/iphone/LinuxCoverFetch.h"

using awtrix::test::check;
using awtrix::test::threadCount;

int main() {
  const std::size_t before = threadCount();
  {
    awtrix::LinuxCoverFetch covers;
    check(!covers.lookup("artist", "title", 8), "a lookup before begin is refused");
    covers.begin();
    check(threadCount() == before, "begin runs no thread");
    covers.stop();
    check(threadCount() == before, "stop has nothing to end");
    check(!covers.lookup("artist", "title", 8), "and a lookup after it is refused");
  }
  check(threadCount() == before, "one that never worked ends cleanly");
  if (awtrix::test::failures()) return 1;
  std::puts("cover fetch: all passed");
  return 0;
}
