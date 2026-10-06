#include "platform/tc002/audio/UrlSounds.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "core/net/Url.h"

namespace awtrix {
namespace tc002 {

namespace {

std::string failureText(const net::FileDownload::Result& result) {
  using Failure = net::FileDownload::Failure;
  switch (result.failure) {
    case Failure::Status: return "HTTP " + std::to_string(result.status);
    case Failure::TooLarge: return "file too large";
    case Failure::NoRoom: return "not enough memory";
    case Failure::Empty: return "empty file";
    default: return "download failed";
  }
}

constexpr char kPrefix[] = "awtrix-sound";

// Folders a runtime that did not end cleanly left in /tmp, which only a reboot would empty.
void removeLeftovers(const std::string& directory) {
  DIR* dir = ::opendir(directory.c_str());
  if (!dir) return;
  while (const dirent* entry = ::readdir(dir)) {
    if (std::strncmp(entry->d_name, kPrefix, sizeof(kPrefix) - 1) != 0) continue;
    const std::string folder = directory + "/" + entry->d_name;
    if (DIR* inner = ::opendir(folder.c_str())) {
      while (const dirent* file = ::readdir(inner))
        if (file->d_name[0] != '.') ::unlink((folder + "/" + file->d_name).c_str());
      ::closedir(inner);
    }
    ::rmdir(folder.c_str());
  }
  ::closedir(dir);
}

}

UrlSounds::UrlSounds(std::string directory, Speaker speaker,
                     std::function<bool(uint64_t&)> available)
    : speaker_(std::move(speaker)), available_(std::move(available)) {
  removeLeftovers(directory);
  const net::FileDownload::Limits limits{kMaxBytes, kTimeoutMs, true,
                                         [this](std::size_t more) { return room(more); }};
  oneShot_.download = std::make_unique<net::FileDownload>(directory, kPrefix, limits);
  loop_.download = std::make_unique<net::FileDownload>(directory, kPrefix, limits);
}

// The files first: each download removes its folder, which only works once it is empty.
UrlSounds::~UrlSounds() {
  forget(oneShot_, loop_);
  forget(loop_, oneShot_);
  oneShot_.download.reset();
  loop_.download.reset();
}

// Download thread: available_ only reads /proc/meminfo.
bool UrlSounds::room(std::size_t more) const {
  uint64_t free = 0;
  return available_ && available_(free) && free >= kKeepFreeBytes && free - kKeepFreeBytes >= more;
}

// The layer lets go of its file; the other layer may still play the same one.
void UrlSounds::forget(Layer& l, const Layer& other) {
  if (!l.file.empty() && l.file != other.file) ::unlink(l.file.c_str());
  l.file.clear();
  l.url.clear();
  l.idleSinceMs = -1;
}

bool UrlSounds::play(const std::string& url, bool loop) {
  const auto parsed = net::parseUrl(url);
  if (!parsed) return false;
  Layer& l = layer(loop);
  Layer& other = layer(!loop);
  if (l.url == url && (l.pending || (loop && speaker_.playing(true) == l.file))) return true;
  struct stat st;
  if (!loop && l.url == url && !l.file.empty() && ::stat(l.file.c_str(), &st) == 0) {
    l.idleSinceMs = -1;
    return speaker_.playOneShot(l.file, url);
  }
  if (loop) speaker_.stopLoop();
  else speaker_.stopOneShot();
  l.download->cancel();
  l.pending = false;
  forget(l, other);
  l.url = url;
  // Fetched already by the other layer: both play the one file.
  if (other.url == url && !other.file.empty()) {
    l.file = other.file;
    return loop ? speaker_.playLoop(l.file) : speaker_.playOneShot(l.file, url);
  }
  l.generation = l.download->start(parsed->origin(), parsed->requestTarget());
  l.seq = speaker_.seq(loop);
  l.pending = true;
  return true;
}

void UrlSounds::stop(bool loop) {
  Layer& l = layer(loop);
  if (!l.pending && loop) return;
  if (l.pending) l.download->cancel();
  l.pending = false;
  forget(l, layer(!loop));
}

void UrlSounds::arrived(bool loop, net::FileDownload::Result& result) {
  Layer& l = layer(loop);
  if (!l.pending || result.generation != l.generation) {
    if (!result.path.empty()) ::unlink(result.path.c_str());
    return;
  }
  l.pending = false;
  if (speaker_.seq(loop) != l.seq) {
    if (!result.path.empty()) ::unlink(result.path.c_str());
    forget(l, layer(!loop));
    return;
  }
  if (!result.success()) {
    forget(l, layer(!loop));
    speaker_.failed(failureText(result), loop);
    return;
  }
  l.file = std::move(result.path);
  if (!(loop ? speaker_.playLoop(l.file) : speaker_.playOneShot(l.file, l.url))) {
    forget(l, layer(!loop));
    speaker_.failed("speaker unavailable", loop);
  }
}

void UrlSounds::tick(int64_t nowMs) {
  net::FileDownload::Result result;
  if (oneShot_.download->poll(result)) arrived(false, result);
  if (loop_.download->poll(result)) arrived(true, result);
  for (bool loop : {false, true}) {
    Layer& l = layer(loop);
    if (l.pending || l.file.empty()) continue;
    if (speaker_.playing(loop) == l.file) {
      l.idleSinceMs = -1;
      continue;
    }
    if (!loop && l.idleSinceMs < 0) l.idleSinceMs = nowMs;
    if (loop || nowMs - l.idleSinceMs >= kKeepMs) forget(l, layer(!loop));
  }
}

}
}
