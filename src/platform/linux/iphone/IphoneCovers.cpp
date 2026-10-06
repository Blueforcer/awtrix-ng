#include "platform/linux/iphone/IphoneCovers.h"

#include "core/api/JsonReader.h"
#include "platform/linux/net/UrlEncode.h"

namespace awtrix::iphone {

std::string coverSearchUrl(const std::string& artist, const std::string& title) {
  return "https://itunes.apple.com/search?term=" + net::percentEncoded(artist + " " + title) + "&entity=song&limit=1";
}

std::string coverImageUrl(const std::string& searchAnswer, int size) {
  api::JsonReader results = api::memberValue(api::JsonReader(searchAnswer), "results");
  if (!results.enterArray() || !results.nextElement()) return "";
  std::string url;
  if (!api::memberValue(results, "artworkUrl100").appendString(url) || url.rfind("https://", 0) != 0) return "";
  const std::string from = "100x100bb";
  const std::size_t at = url.rfind(from);
  if (at == std::string::npos) return "";
  const std::string side = std::to_string(size);
  return url.replace(at, from.size(), side + "x" + side + "bb");
}

std::string CoverCache::key(const std::string& artist, const std::string& title, int size) {
  return artist + '\0' + title + '\0' + std::to_string(size);
}

bool CoverCache::find(const std::string& key, std::string& url) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->first != key) continue;
    entries_.splice(entries_.begin(), entries_, it);
    url = entries_.front().second;
    return true;
  }
  return false;
}

void CoverCache::put(const std::string& key, std::string url) {
  for (auto it = entries_.begin(); it != entries_.end(); ++it)
    if (it->first == key) {
      entries_.erase(it);
      break;
    }
  entries_.emplace_front(key, std::move(url));
  if (entries_.size() > kEntries) entries_.pop_back();
}

}
