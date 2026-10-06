#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "platform/linux/net/FileDownload.h"

namespace awtrix {
namespace tc002 {

// MP3s from a URL, fetched whole into /tmp (at most kMaxBytes, kKeepFreeBytes free).
// One file per layer; render thread only.
class UrlSounds {
 public:
  static constexpr std::size_t kMaxBytes = 4u << 20;
  static constexpr uint64_t kKeepFreeBytes = 4u << 20;
  static constexpr int64_t kTimeoutMs = 60000;
  static constexpr int64_t kKeepMs = 2000;

  // The speaker side. oneShotSeq and loopSeq count every change to the layer, so a download that
  // finishes after someone else took the layer is dropped. playing names the file the layer plays
  // now, "" when it plays none.
  struct Speaker {
    std::function<bool(const std::string& file, const std::string& url)> playOneShot;
    std::function<bool(const std::string& file)> playLoop;
    std::function<void()> stopOneShot;
    std::function<void()> stopLoop;
    std::function<uint32_t(bool loop)> seq;
    std::function<std::string(bool loop)> playing;
    std::function<void(const std::string& error, bool loop)> failed;
  };

  // available reports MemAvailable; false leaves it unknown and refuses the download.
  UrlSounds(std::string directory, Speaker speaker, std::function<bool(uint64_t&)> available);
  ~UrlSounds();

  bool play(const std::string& url, bool loop);
  // Drops the layer's download, and the one-shot's file; the sound itself is stopped by the
  // speaker.
  void stop(bool loop);
  // The layer's sound is still being fetched.
  bool pending(bool loop) const { return loop ? loop_.pending : oneShot_.pending; }
  void tick(int64_t nowMs);

 private:
  struct Layer {
    std::unique_ptr<net::FileDownload> download;
    uint64_t generation = 0;
    bool pending = false;
    uint32_t seq = 0;
    std::string url;
    std::string file;
    // When the speaker was first seen not playing file; -1 while it plays it.
    int64_t idleSinceMs = -1;
  };

  Layer& layer(bool loop) { return loop ? loop_ : oneShot_; }
  void forget(Layer& l, const Layer& other);
  void arrived(bool loop, net::FileDownload::Result& result);
  bool room(std::size_t bytes) const;

  Speaker speaker_;
  std::function<bool(uint64_t&)> available_;
  Layer oneShot_, loop_;
};

}
}
