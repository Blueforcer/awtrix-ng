#pragma once

#include <string>

namespace awtrix::tc002::update {

// Not synchronized: callers run whole UpdateState operations on a store one at a
// time and never start one from inside a store call, and only one store instance
// may be open on a document at once. FileStateStore enforces the latter with
// flock; another implementation must provide the same exclusion.
class StateStore {
 public:
  virtual ~StateStore() = default;
  // False only on an I/O failure. A missing file returns true with exists=false.
  virtual bool read(std::string& json, bool& exists, std::string& error) = 0;
  // Durable replacement of the whole document. False means not durable.
  virtual bool write(const std::string& json, std::string& error) = 0;
};


}  // namespace awtrix::tc002::update
