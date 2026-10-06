#pragma once

namespace awtrix::ha {
// A platform discovery component; emit() adds its base topic and unique ID.
struct Entity {
  const char* key;
  const char* body;
};
}
