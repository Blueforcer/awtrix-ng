#pragma once

#include <cstddef>
#include <string>

namespace awtrix {
namespace mqtt {

// MQTT 3.1.1 topic filter matching, level by level and without allocating.
inline bool topicMatches(const std::string& filter, const std::string& topic) {
  if (filter.empty() || topic.empty()) return false;
  // A leading wildcard must not sweep up the broker's own $SYS tree.
  if (topic[0] == '$' && (filter[0] == '+' || filter[0] == '#')) return false;

  std::size_t f = 0;
  std::size_t t = 0;
  bool topicExhausted = false;

  for (;;) {
    const std::size_t fSep = filter.find('/', f);
    const bool lastFilterLevel = (fSep == std::string::npos);
    const std::size_t fEnd = lastFilterLevel ? filter.size() : fSep;
    const std::size_t fLen = fEnd - f;

    if (fLen == 1 && filter[f] == '#') return lastFilterLevel;

    if (topicExhausted) return false;

    const std::size_t tSep = topic.find('/', t);
    const bool lastTopicLevel = (tSep == std::string::npos);
    const std::size_t tEnd = lastTopicLevel ? topic.size() : tSep;

    if (!(fLen == 1 && filter[f] == '+')) {
      if (fLen != tEnd - t) return false;
      if (topic.compare(t, tEnd - t, filter, f, fLen) != 0) return false;
    }

    if (lastFilterLevel) return lastTopicLevel;
    // Run one level past the end of the topic instead of failing here, so that "a/#" still matches
    // the bare topic "a" as the spec requires.
    if (lastTopicLevel)
      topicExhausted = true;
    else
      t = tEnd + 1;
    f = fEnd + 1;
  }
}

}
}
