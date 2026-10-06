#include "platform/tc002/audio/Tc002AudioStream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "core/net/Url.h"
#include "core/audio/StreamDecode.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/IcyStream.h"
#include "core/radio/PlaylistParser.h"
#include "core/radio/StationUrl.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/net/HttpClient.h"
#include "system/MonotonicClock.h"

namespace awtrix {
namespace tc002 {

struct StreamSource::Shared {
  std::mutex mutex;
  std::condition_variable changed;
  audio::StreamInputBuffer input{kRingBytes};
  uint32_t connection = 0;
  bool streaming = false;
  bool cancelled = false;
  std::string title;
  std::string error;
  bool titleNew = false;
  bool errorNew = false;
  net::SocketInterrupt interrupt;
};

namespace {

constexpr int kConnectTimeoutS = 5;

std::atomic<int> g_networkThreads{0};
std::mutex g_threadsMutex;
std::condition_variable g_threadsDone;

using Shared = StreamSource::Shared;

bool cancelled(Shared& shared) {
  std::lock_guard<std::mutex> lock(shared.mutex);
  return shared.cancelled;
}

bool pause(Shared& shared, int ms) {
  std::unique_lock<std::mutex> lock(shared.mutex);
  return !shared.changed.wait_for(lock, std::chrono::milliseconds(ms),
                                  [&] { return shared.cancelled; });
}

void publishError(Shared& shared, const std::string& message) {
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.error = message;
  shared.errorNew = true;
  shared.streaming = false;
}

void publishTitle(Shared& shared, const std::string& title) {
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.title = title;
  shared.titleNew = true;
}

void beginConnection(Shared& shared) {
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.input.clear();
  ++shared.connection;
  shared.streaming = true;
  shared.error.clear();
  shared.errorNew = true;
  shared.changed.notify_all();
}

// Blocks while the ring is full: the socket then fills and TCP slows the station down.
bool push(Shared& shared, const uint8_t* data, std::size_t bytes) {
  std::unique_lock<std::mutex> lock(shared.mutex);
  while (bytes) {
    shared.changed.wait(lock, [&] { return shared.cancelled || shared.input.room(); });
    if (shared.cancelled) return false;
    const std::size_t room = std::min(bytes, shared.input.room());
    shared.input.push(data, room);
    data += room;
    bytes -= room;
  }
  return true;
}

enum class Fetch : uint8_t { Cancelled, Failed, Playlist, Ended };

Fetch fetch(Shared& shared, const net::Url& target, bool urlLooksLikePlaylist, std::string& body) {
  httplib::Client client(target.origin());
  net::configure(client, {0, kConnectTimeoutS, true, radio::kReadTimeoutMs / 1000});
  shared.interrupt.watch(client);
  if (cancelled(shared)) return Fetch::Cancelled;
  bool accepted = false;
  bool playlist = false;
  radio::MetadataSplitter splitter;
  radio::TitleTracker titles;
  const httplib::Headers headers{{"Icy-MetaData", "1"}, {"User-Agent", "AWTRIX-NG"}};
  client.Get(
      target.requestTarget(), headers,
      [&](const httplib::Response& response) {
        if (response.status != 200 || cancelled(shared)) return false;
        const std::string type = response.get_header_value("Content-Type");
        playlist = radio::isPlaylistType(type) ||
                   (urlLooksLikePlaylist && type.find("audio/") == std::string::npos);
        accepted = true;
        if (!playlist) {
          splitter.reset(std::atoi(response.get_header_value("icy-metaint").c_str()));
          beginConnection(shared);
        }
        return true;
      },
      [&](const char* data, std::size_t bytes) {
        if (playlist) {
          if (body.size() + bytes > radio::kMaxPlaylistBytes) return false;
          body.append(data, bytes);
          return true;
        }
        bool okay = true;
        splitter.feed(
            reinterpret_cast<const uint8_t*>(data), bytes,
            [&](const uint8_t* audio, std::size_t n) {
              if (okay) okay = push(shared, audio, n);
            },
            [&](const std::string& block) {
              if (titles.update(block)) publishTitle(shared, titles.title());
            });
        return okay;
      });
  shared.interrupt.release();
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    shared.streaming = false;
    if (shared.cancelled) return Fetch::Cancelled;
  }
  if (!accepted) return Fetch::Failed;
  return playlist ? Fetch::Playlist : Fetch::Ended;
}

void runNetwork(std::shared_ptr<Shared> shared, std::string station) {
  int attempt = 0;
  radio::StationUrl url(std::move(station));
  while (!cancelled(*shared)) {
    const auto target = net::parseUrl(url.current());
    if (!target) {
      publishError(*shared, "invalid URL");
      url.restart();
      pause(*shared, radio::kBackoffMs[2]);
      continue;
    }
    std::string body;
    const Fetch result =
        fetch(*shared, *target, radio::kindFromUrl(url.current()) != radio::PlaylistKind::None, body);
    if (result == Fetch::Cancelled) break;
    if (result == Fetch::Playlist) {
      if (url.follow(body)) continue;
      publishError(*shared, "empty playlist");
      pause(*shared, radio::kBackoffMs[2]);
      continue;
    }
    url.restart();
    if (result == Fetch::Failed) {
      publishError(*shared, "connect failed");
      pause(*shared, radio::kBackoffMs[attempt]);
      if (attempt < 2) ++attempt;
      continue;
    }
    attempt = 0;
    pause(*shared, radio::kBackoffMs[0]);
  }
  std::lock_guard<std::mutex> lock(g_threadsMutex);
  --g_networkThreads;
  g_threadsDone.notify_all();
}

}

StreamSource::StreamSource(const std::string& url)
    : shared_(std::make_shared<Shared>()),
      decoder_(new mp3::Decoder()),
      pcm_(mp3::kMaxPcmPerFrame) {
  ++g_networkThreads;
  try {
    std::thread(runNetwork, shared_, url).detach();
  } catch (...) {
    --g_networkThreads;
    fatal_ = true;
  }
}

StreamSource::~StreamSource() {
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->cancelled = true;
  }
  shared_->interrupt.interrupt();
  shared_->changed.notify_all();
}

bool StreamSource::waitForNetworkThreads(int timeoutMs) {
  std::unique_lock<std::mutex> lock(g_threadsMutex);
  return g_threadsDone.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                [] { return g_networkThreads.load() == 0; });
}

bool StreamSource::takeTitle(std::string& out) {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  if (!shared_->titleNew) return false;
  shared_->titleNew = false;
  out = shared_->title;
  return true;
}

bool StreamSource::takeError(std::string& out) {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  if (!shared_->errorNew) return false;
  shared_->errorNew = false;
  out = shared_->error;
  return true;
}

bool StreamSource::connected() const {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  return shared_->streaming;
}

PcmSource::Read StreamSource::next(const int16_t*& samples, std::size_t& frames) {
  if (fatal_) return Read::Error;
  std::unique_lock<std::mutex> lock(shared_->mutex);
  auto& input = shared_->input;
  if (shared_->connection != seenConnection_) {
    seenConnection_ = shared_->connection;
    decoder_->reset();
    bytesSeen_ = 0;
    prerolled_ = false;
    decodedAnything_ = false;
  }
  buffered_ = static_cast<uint32_t>(input.size());
  if (!prerolled_) {
    if (buffered_ < kPrerollBytes) return Read::Wait;
    prerolled_ = true;
  }
  const int64_t now = monotonicMs();
  bool decoded = false;
  int64_t started = 0;
  const auto decode = [&](const audio::StreamInputBuffer::View& view) {
    started = awtrix::monotonicUs();
    return decoder_->decode(view.data, view.size, pcm_.data());
  };
  const auto frame = [&](const mp3::DecodeResult& result) {
    if (result.samples <= 0 || result.channels < 1 || result.channels > 2) return true;
    const uint32_t took = static_cast<uint32_t>(awtrix::monotonicUs() - started);
    decodeUs_ = decodeUs_ ? (decodeUs_ * 7 + took) / 8 : took;
    decodedAnything_ = decoded = true;
    starvedSinceMs_ = -1;
    rate_ = static_cast<uint32_t>(result.sampleRateHz);
    channels_ = static_cast<uint8_t>(result.channels);
    samples = pcm_.data();
    frames = static_cast<std::size_t>(result.samples);
    return false;
  };
  const auto before = input.size();
  audio::decodeFrames(input, 1, decode, frame);
  const auto consumed = before - input.size();
  bytesSeen_ += consumed;
  buffered_ = static_cast<uint32_t>(input.size());
  if (consumed) shared_->changed.notify_all();
  lock.unlock();
  if (decoded) return Read::Data;
  if (!decodedAnything_ && bytesSeen_ > kUndecodableAfterBytes) {
    publishError(*shared_, "not playable MP3");
    fatal_ = true;
    return Read::Error;
  }
  if (starvedSinceMs_ >= 0) starvedMs_ += static_cast<uint32_t>(now - starvedSinceMs_);
  starvedSinceMs_ = now;
  return Read::Wait;
}

}
}
