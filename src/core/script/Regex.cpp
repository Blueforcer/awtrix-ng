#include "core/script/Regex.h"

#include <cstring>

namespace awtrix::script {

namespace {

struct Range {
  uint8_t lo, hi;
};

const Range kDigit[] = {{'0', '9'}};
const Range kWord[] = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
const Range kSpace[] = {{'\t', '\r'}, {' ', ' '}};

}


class RegexParser {
 public:
  RegexParser(Regex& re, const std::string& pat) : re_(re), pat_(pat) {}

  bool parse() {
    // A lazy `.*` loop, kPrefixBytes long, so an unanchored search can start anywhere. Lazy so
    // it gives up the shortest run first and the match stays leftmost; match() jumps past it.
    emit(Regex::kRSplit);
    emitOff(4);
    emit(Regex::kAny);
    emit(Regex::kJmp);
    emitOff(-7);

    emit(Regex::kSave);
    emit(0);
    if (!alternation(0)) return false;
    if (pos_ < pat_.size()) return false;
    emit(Regex::kSave);
    emit(1);
    emit(Regex::kMatch);
    return !failed_;
  }

  int groups() const { return nGroups_ + 1; }

 private:

  std::size_t here() const { return re_.prog_.size(); }

  void emit(uint8_t b) {
    if (re_.prog_.size() >= Regex::kMaxProgram) {
      failed_ = true;
      return;
    }
    re_.prog_.push_back(b);
  }

  void emitOff(int off) {
    emit(static_cast<uint8_t>(off & 0xFF));
    emit(static_cast<uint8_t>((off >> 8) & 0xFF));
  }

  // Writes the 16-bit jump offset at `at`. Offsets are relative to the byte after the
  // operand pair, which is where the VM's pc has already advanced to when it applies them.
  void patch(std::size_t at, std::size_t target) {
    const int off = static_cast<int>(target) - static_cast<int>(at + 2);
    re_.prog_[at] = static_cast<uint8_t>(off & 0xFF);
    re_.prog_[at + 1] = static_cast<uint8_t>((off >> 8) & 0xFF);
  }

  // A quantifier is parsed after what it applies to, so its fork is spliced in ahead of code
  // already emitted. Everything past `at` shifts by 3 bytes, which callers re-patch for.
  bool insertFork(std::size_t at, uint8_t op) {
    if (re_.prog_.size() + 3 > Regex::kMaxProgram) {
      failed_ = true;
      return false;
    }
    const uint8_t blank[3] = {op, 0, 0};
    re_.prog_.insert(re_.prog_.begin() + at, blank, blank + 3);
    return true;
  }


  bool atEnd() const { return pos_ >= pat_.size(); }
  char peek() const { return pat_[pos_]; }
  bool eat(char c) {
    if (atEnd() || pat_[pos_] != c) return false;
    ++pos_;
    return true;
  }


  bool alternation(int depth) {
    if (depth > Regex::kMaxDepth) return false;

    const std::size_t start = here();
    if (!concat(depth)) return false;

    std::vector<std::size_t> exits;
    while (eat('|')) {
      if (!insertFork(start, Regex::kSplit)) return false;
      for (auto& e : exits) e += 3;

      emit(Regex::kJmp);
      exits.push_back(here());
      emitOff(0);

      patch(start + 1, here());
      if (!concat(depth)) return false;
    }
    for (const auto e : exits) patch(e, here());
    return !failed_;
  }

  bool concat(int depth) {
    while (!atEnd() && peek() != '|' && peek() != ')') {
      if (!repeat(depth)) return false;
    }
    return !failed_;
  }

  // Applies a trailing *, + or ? to what single() just emitted. Greedy versus lazy is only the
  // choice between kSplit, which prefers to keep consuming, and kRSplit, which prefers to stop.
  bool repeat(int depth) {
    const std::size_t start = here();
    if (!single(depth)) return false;

    if (atEnd()) return true;
    const char q = peek();
    if (q != '*' && q != '+' && q != '?') return true;
    ++pos_;
    const bool lazy = eat('?');

    if (q == '+') {
      emit(lazy ? Regex::kSplit : Regex::kRSplit);
      const std::size_t op = here();
      emitOff(0);
      patch(op, start);
      return !failed_;
    }

    if (q == '?') {
      if (!insertFork(start, lazy ? Regex::kRSplit : Regex::kSplit)) return false;
      patch(start + 1, here());
      return !failed_;
    }

    if (!insertFork(start, lazy ? Regex::kRSplit : Regex::kSplit)) return false;
    emit(Regex::kJmp);
    const std::size_t jmp = here();
    emitOff(0);
    patch(jmp, start);
    patch(start + 1, here());
    return !failed_;
  }

  bool single(int depth) {
    if (atEnd()) return false;
    const char c = pat_[pos_++];

    switch (c) {
      case '(': {
        ++nGroups_;
        if (nGroups_ >= Regex::kMaxGroups) return false;
        const int gid = nGroups_;
        emit(Regex::kSave);
        emit(static_cast<uint8_t>(gid * 2));
        if (!alternation(depth + 1)) return false;
        if (!eat(')')) return false;
        emit(Regex::kSave);
        emit(static_cast<uint8_t>(gid * 2 + 1));
        return !failed_;
      }
      case '[':
        return charClass();
      case '.':
        emit(Regex::kAny);
        return !failed_;
      case '^':
        emit(Regex::kBol);
        return !failed_;
      case '$':
        emit(Regex::kEol);
        return !failed_;
      case '*':
      case '+':
      case '?':
      case ')':
      case '|':
        return false;
      case '\\':
        return escape();
      default:
        emit(Regex::kChar);
        emit(static_cast<uint8_t>(c));
        return !failed_;
    }
  }

  bool escape() {
    if (atEnd()) return false;
    const char c = pat_[pos_++];
    switch (c) {
      case 'd': return emitClass(false, kDigit, 1);
      case 'D': return emitClass(true, kDigit, 1);
      case 'w': return emitClass(false, kWord, 4);
      case 'W': return emitClass(true, kWord, 4);
      case 's': return emitClass(false, kSpace, 2);
      case 'S': return emitClass(true, kSpace, 2);
      case 'n': emit(Regex::kChar); emit('\n'); return !failed_;
      case 'r': emit(Regex::kChar); emit('\r'); return !failed_;
      case 't': emit(Regex::kChar); emit('\t'); return !failed_;
      default:
        emit(Regex::kChar);
        emit(static_cast<uint8_t>(c));
        return !failed_;
    }
  }

  bool emitClass(bool neg, const Range* ranges, int n) {
    emit(Regex::kClass);
    emit(neg ? 1 : 0);
    emit(static_cast<uint8_t>(n));
    for (int i = 0; i < n; ++i) {
      emit(ranges[i].lo);
      emit(ranges[i].hi);
    }
    return !failed_;
  }

  bool charClass() {
    bool neg = eat('^');
    std::vector<Range> ranges;

    while (!atEnd() && peek() != ']') {
      uint8_t lo;
      const char c = pat_[pos_++];
      if (c == '\\') {
        if (atEnd()) return false;
        const char e = pat_[pos_++];
        const Range* named = nullptr;
        int n = 0;
        switch (e) {
          case 'd': named = kDigit; n = 1; break;
          case 'w': named = kWord; n = 4; break;
          case 's': named = kSpace; n = 2; break;
          case 'n': lo = '\n'; break;
          case 'r': lo = '\r'; break;
          case 't': lo = '\t'; break;
          default: lo = static_cast<uint8_t>(e); break;
        }
        if (named) {
          for (int i = 0; i < n; ++i) ranges.push_back(named[i]);
          continue;
        }
      } else {
        lo = static_cast<uint8_t>(c);
      }

      uint8_t hi = lo;
      if (!atEnd() && peek() == '-' && pos_ + 1 < pat_.size() && pat_[pos_ + 1] != ']') {
        ++pos_;
        const char r = pat_[pos_++];
        hi = static_cast<uint8_t>(r == '\\' ? (atEnd() ? 0 : pat_[pos_++]) : r);
        if (hi < lo) return false;
      }
      ranges.push_back({lo, hi});
      if (ranges.size() > 32) return false;
    }
    if (!eat(']')) return false;
    if (ranges.empty()) return false;
    return emitClass(neg, ranges.data(), static_cast<int>(ranges.size()));
  }

  Regex& re_;
  const std::string& pat_;
  std::size_t pos_ = 0;
  int nGroups_ = 0;
  bool failed_ = false;
};


bool Regex::compile(const std::string& pattern) {
  prog_.clear();
  if (pattern.size() > kMaxPattern) return false;

  RegexParser p(*this, pattern);
  if (!p.parse()) {
    prog_.clear();
    return false;
  }
  nGroups_ = p.groups();

  const std::size_t n = prog_.size();
  list_[0].clear();
  list_[1].clear();
  list_[0].reserve(n);
  list_[1].reserve(n);
  stack_.clear();
  stack_.reserve(n);
  seen_.assign(n, 0);
  gen_ = 0;
  return true;
}

bool Regex::search(const std::string& text, Span* groups, int ngroups) {
  return run(text, 0, false, groups, ngroups);
}

bool Regex::searchFrom(const std::string& text, std::size_t from, Span* groups,
                       int ngroups) {
  if (from > text.size()) return false;
  return run(text, from, false, groups, ngroups);
}

bool Regex::match(const std::string& text, Span* groups, int ngroups) {
  return run(text, 0, true, groups, ngroups);
}

// Follows every branch that consumes no input from pc, adding threads that reach a consuming
// opcode to `list`. seen_/gen_ blocks a repeated pc, which is what stops `(a*)*` looping.
void Regex::addThread(std::vector<Thread>& list, uint16_t pc, const int16_t* caps,
                      std::size_t pos, std::size_t textLen) {
  stack_.push_back(Thread{pc, {}});
  std::memcpy(stack_.back().caps, caps, sizeof(int16_t) * kSlots);

  while (!stack_.empty()) {
    Thread t = stack_.back();
    stack_.pop_back();
    if (seen_[t.pc] == gen_) continue;
    seen_[t.pc] = gen_;

    const uint8_t op = prog_[t.pc];
    switch (op) {
      case kJmp: {
        const int off = static_cast<int16_t>(prog_[t.pc + 1] | (prog_[t.pc + 2] << 8));
        t.pc = static_cast<uint16_t>(t.pc + 3 + off);
        stack_.push_back(t);
        break;
      }
      case kSplit:
      case kRSplit: {
        const int off = static_cast<int16_t>(prog_[t.pc + 1] | (prog_[t.pc + 2] << 8));
        Thread fall = t;
        fall.pc = static_cast<uint16_t>(t.pc + 3);
        Thread jump = t;
        jump.pc = static_cast<uint16_t>(t.pc + 3 + off);
        if (op == kSplit) {
          stack_.push_back(jump);
          stack_.push_back(fall);
        } else {
          stack_.push_back(fall);
          stack_.push_back(jump);
        }
        break;
      }
      case kSave: {
        const uint8_t slot = prog_[t.pc + 1];
        if (slot < kSlots) t.caps[slot] = static_cast<int16_t>(pos);
        t.pc += 2;
        stack_.push_back(t);
        break;
      }
      case kBol:
        if (pos == 0) {
          t.pc += 1;
          stack_.push_back(t);
        }
        break;
      case kEol:
        if (pos == textLen) {
          t.pc += 1;
          stack_.push_back(t);
        }
        break;
      default:
        list.push_back(t);
        break;
    }
  }
}

namespace {

bool classMatch(const uint8_t* p, uint8_t b) {
  const bool neg = p[0] != 0;
  const int n = p[1];
  bool in = false;
  for (int i = 0; i < n; ++i) {
    if (b >= p[2 + i * 2] && b <= p[3 + i * 2]) {
      in = true;
      break;
    }
  }
  return in != neg;
}

}

// One pass with all live threads in lockstep. addThread built the list in preference order,
// so the first thread to reach kMatch is the one a backtracking engine would have found.
bool Regex::run(const std::string& text, std::size_t from, bool anchored, Span* groups,
                int ngroups) {
  if (prog_.empty()) return false;
  if (text.size() > kMaxInput) return false;

  const std::size_t n = text.size();
  std::vector<Thread>& clist = list_[0];
  std::vector<Thread>& nlist = list_[1];
  clist.clear();
  nlist.clear();

  int16_t caps[kSlots];
  for (int i = 0; i < kSlots; ++i) caps[i] = -1;

  bool matched = false;
  int16_t best[kSlots];

  // Invalidates seen_ in O(1) per input position instead of clearing it. Only the wrap of the
  // 8-bit counter costs a real pass, and 0 is kept as "never marked".
  auto bumpGen = [this] {
    if (++gen_ == 0) {
      std::fill(seen_.begin(), seen_.end(), 0);
      gen_ = 1;
    }
  };

  bumpGen();
  addThread(clist, anchored ? static_cast<uint16_t>(kPrefixBytes) : 0, caps, from, n);

  for (std::size_t sp = from; sp <= n; ++sp) {
    if (clist.empty()) break;
    bumpGen();
    for (std::size_t i = 0; i < clist.size(); ++i) {
      const Thread& t = clist[i];
      const uint8_t op = prog_[t.pc];

      if (op == kMatch) {
        std::memcpy(best, t.caps, sizeof(best));
        matched = true;
        break;
      }
      if (sp >= n) continue;

      const uint8_t b = static_cast<uint8_t>(text[sp]);
      switch (op) {
        case kChar:
          if (b == prog_[t.pc + 1])
            addThread(nlist, static_cast<uint16_t>(t.pc + 2), t.caps, sp + 1, n);
          break;
        case kAny:
          addThread(nlist, static_cast<uint16_t>(t.pc + 1), t.caps, sp + 1, n);
          break;
        case kClass: {
          const uint8_t* cls = prog_.data() + t.pc + 1;
          if (classMatch(cls, b)) {
            const uint16_t next = static_cast<uint16_t>(t.pc + 3 + cls[1] * 2);
            addThread(nlist, next, t.caps, sp + 1, n);
          }
          break;
        }
      }
    }
    clist.swap(nlist);
    nlist.clear();
  }

  if (!matched) return false;
  for (int g = 0; g < ngroups; ++g) {
    if (g < kMaxGroups && best[g * 2] >= 0 && best[g * 2 + 1] >= 0) {
      groups[g].begin = best[g * 2];
      groups[g].end = best[g * 2 + 1];
    } else {
      groups[g] = Span{};
    }
  }
  return true;
}

}
