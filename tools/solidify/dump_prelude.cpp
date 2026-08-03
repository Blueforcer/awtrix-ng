
#include <cstdio>
#include <cstring>

#include "core/script/Prelude.h"

int main() {
  const char* p = awtrix::script::kPrelude;
  if (!p) return 1;
  std::fwrite(p, 1, std::strlen(p), stdout);
  return 0;
}
