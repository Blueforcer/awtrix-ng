#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix::script {

// Thompson/Pike NFA simulation: all threads advance together in one pass, so runtime is
// linear and no pattern can stall the render loop. No backreferences, no lookaround.
class Regex {
 public:
  static constexpr std::size_t kMaxPattern = 256;
  static constexpr int kMaxGroups = 8;
  // Capture offsets are held in int16_t, so the input has to stay inside its positive range.
  static constexpr std::size_t kMaxInput = 32000;

  // Byte offsets into the subject, half-open. Both -1 for a group that did not participate.
  struct Span {
    int begin = -1;
    int end = -1;
  };

  bool compile(const std::string& pattern);
  bool ok() const { return !prog_.empty(); }

  int groupCount() const { return nGroups_; }

  bool search(const std::string& text, Span* groups, int ngroups);

  bool searchFrom(const std::string& text, std::size_t from, Span* groups, int ngroups);

  bool match(const std::string& text, Span* groups, int ngroups);

 private:
  static constexpr std::size_t kMaxProgram = 512;
  static constexpr int kMaxDepth = 16;
  static constexpr int kSlots = kMaxGroups * 2;
  // Size of the lazy `.*` the compiler puts in front of every program to make an unanchored
  // search work. An anchored match starts at this offset to skip it.
  static constexpr std::size_t kPrefixBytes = 7;

  enum Op : uint8_t {
    kChar = 1,
    kAny,
    kClass,
    kJmp,
    kSplit,
    kRSplit,
    kSave,
    kBol,
    kEol,
    kMatch,
  };

  struct Thread {
    uint16_t pc;
    int16_t caps[kSlots];
  };

  bool run(const std::string& text, std::size_t from, bool anchored, Span* groups,
           int ngroups);
  void addThread(std::vector<Thread>& list, uint16_t pc, const int16_t* caps,
                 std::size_t pos, std::size_t textLen);

  friend class RegexParser;

  std::vector<uint8_t> prog_;
  int nGroups_ = 0;

  // Scratch reused across runs so matching allocates nothing: current and next thread lists,
  // the epsilon-closure work stack, and a per-step "already added this pc" marker.
  std::vector<Thread> list_[2];
  std::vector<Thread> stack_;
  std::vector<uint8_t> seen_;
  uint8_t gen_ = 0;
};

}
