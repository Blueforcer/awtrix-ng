#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <utility>

// Album covers for the now-playing app. Neither iPhone service carries pictures, so the display
// asks the iTunes Search API for the track and shows the cover it names, in the size of an icon.
namespace awtrix::iphone {

std::string coverSearchUrl(const std::string& artist, const std::string& title);
// The first result's cover at size x size pixels; "" when the answer names none.
std::string coverImageUrl(const std::string& searchAnswer, int size);

// Looks covers up one at a time, off the main loop.
class CoverSource {
 public:
  virtual ~CoverSource() = default;
  // False while a lookup is under way.
  virtual bool lookup(const std::string& artist, const std::string& title, int size) = 0;
  // The finished lookup's cover URL, "" when there is none; true once per lookup.
  virtual bool result(std::string& url) = 0;
};

// The covers looked up lately, the tracks without one included, least recently used first out.
class CoverCache {
 public:
  static constexpr std::size_t kEntries = 16;

  static std::string key(const std::string& artist, const std::string& title, int size);
  bool find(const std::string& key, std::string& url);
  void put(const std::string& key, std::string url);

 private:
  std::list<std::pair<std::string, std::string>> entries_;
};

}
