#include "../../support.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "platform/tc002/voice/ClipStore.h"
#include "platform/tc002/voice/WavWriter.h"

using namespace awtrix::tc002::voice;
using awtrix::MemoryRoom;
using awtrix::tc002::ClipKind;
using awtrix::test::check;
using awtrix::test::require;

namespace {

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool exists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

int lowestFreeDescriptor() {
  const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  ::close(fd);
  return fd;
}

constexpr uint64_t kPlenty = uint64_t{1} << 40;

MemoryRoom available(const uint64_t& bytes) {
  return MemoryRoom([&bytes](uint64_t& out) {
    out = bytes;
    return true;
  }, 1000);
}

uint32_t le32(const std::string& bytes, std::size_t at) {
  uint32_t value = 0;
  for (int i = 3; i >= 0; --i) value = value << 8 | static_cast<uint8_t>(bytes[at + static_cast<std::size_t>(i)]);
  return value;
}

void wavFile(const std::string& folder) {
  const std::string path = folder + "/a.wav";
  WavWriter wav;
  require(wav.open(path, 16000), "the writer creates its file");
  const std::vector<int16_t> samples{1, -2, 300, -32768, 32767};
  check(wav.write(samples.data(), 3) && wav.write(samples.data() + 3, 2) && wav.samples() == 5, "samples append");
  require(wav.close(), "close fills in the sizes");
  const std::string bytes = readAll(path);
  check(bytes.size() == 44 + 10 && bytes.compare(0, 4, "RIFF") == 0 && le32(bytes, 4) == 36 + 10 &&
            bytes.compare(8, 8, "WAVEfmt ") == 0 && le32(bytes, 24) == 16000 && le32(bytes, 28) == 32000 &&
            bytes.compare(36, 4, "data") == 0 && le32(bytes, 40) == 10,
        "a RIFF/WAVE header for PCM16 mono at 16 kHz with the right sizes");
  check(static_cast<uint8_t>(bytes[48]) == 0x2c && static_cast<uint8_t>(bytes[49]) == 0x01 &&
            static_cast<uint8_t>(bytes[50]) == 0x00 && static_cast<uint8_t>(bytes[51]) == 0x80,
        "samples are little-endian");
  check(wavHeader(5, 16000) == bytes.substr(0, 44) && wavBytes(5) == bytes.size(), "wavHeader() is the same header");
  check(wav.close() && !wav.write(samples.data(), 1), "a closed writer closes again and takes nothing");
  check(!wav.open(folder + "/missing/b.wav", 16000), "a file that cannot be made is refused");
  const int lowest = lowestFreeDescriptor();
  check(!wav.open("/dev/full", 16000) && lowestFreeDescriptor() == lowest && wav.close(),
        "a file whose header cannot be written is refused and closed");
  ::unlink(path.c_str());
}

void limits(const std::string& folder) {
  ClipStore store(folder, available(kPlenty));
  require(store.ready(), "the store makes its folder");
  ClipId ids[4];
  for (ClipId& id : ids) id = store.reserve("A", 1000);
  check(ids[0] && ids[1] && ids[2] && ids[3] && ids[0] != ids[1], "four clips per app");
  for (const ClipId id : ids)
    check(id <= ClipId{INT64_MAX} && id + 1 != ids[0] && id + 1 != ids[1] && id + 1 != ids[2] && id + 1 != ids[3],
          "ids are drawn at random, positive as a script's integers, and do not count up: " + std::to_string(id));
  check(store.reserve("A", 1000) == 0, "a fifth is refused");
  check(store.reserve("B", ClipStore::kBytesPerApp) != 0, "other apps have a limit of their own");
  check(store.reserve("B", 1) == 0, "the bytes of an app are limited to 4 MB");
  store.release("A", ids[0]);
  check(store.reserve("A", 1000) != 0, "a released clip frees its place");
}

void memory(const std::string& folder) {
  uint64_t free = 1000 + 500;
  ClipStore store(folder, available(free));
  check(store.reserve("A", 501) == 0, "a clip that would eat into the memory kept free is refused");
  check(store.reserve("A", 500) != 0, "one that fits beside it is reserved");
  free = 999;
  check(store.reserve("A", 0) == 0, "below the margin nothing fits");
  ClipStore unknown(folder, MemoryRoom({}, 0));
  check(unknown.reserve("A", 1) == 0, "memory that cannot be read refuses every clip");
}

void commitAndRelease(const std::string& parent) {
  std::string folder;
  {
    ClipStore store(parent, available(kPlenty));
    const ClipId id = store.reserve("A", 100);
    const std::string path = store.path(id);
    folder = path.substr(0, path.rfind('/'));
    check(store.find("A", id) == nullptr, "a reserved clip is not usable yet");
    std::ofstream(path, std::ios::binary) << std::string(80, 'x');
    store.releaseReady("A", id);
    check(exists(path) && !store.path(id).empty(), "a script cannot release a clip still being filled");
    require(store.commit(id, 80, 5, ClipKind::Wav), "a clip within its reservation becomes ready");
    const Clip* clip = store.find("A", id);
    check(clip && clip->bytes == 80 && clip->ms == 5 && clip->kind == ClipKind::Wav,
          "it keeps size, length and kind");
    check(store.find("B", id) == nullptr, "another app does not find it");
    store.release("B", id);
    store.releaseReady("B", id);
    check(exists(path), "another app cannot release it");
    store.releaseReady("A", id);
    check(!exists(path) && store.find("A", id) == nullptr, "its app's script releases it and its file");
    const ClipId reserved = store.reserve("A", 100);
    std::ofstream(store.path(reserved), std::ios::binary) << "x";
    const std::string reservedPath = store.path(reserved);
    store.release("A", reserved);
    check(!exists(reservedPath) && store.path(reserved).empty(), "the module releases a reservation with its file");
    const ClipId big = store.reserve("A", 100);
    check(!store.commit(big, 101, 1, ClipKind::Mp3) && store.path(big).empty(),
          "a clip that outgrew its reservation is released");
    const ClipId ready = store.reserve("A", 10), pending = store.reserve("A", 10);
    std::ofstream(store.path(ready)) << "x";
    store.commit(ready, 1, 0, ClipKind::Wav);
    store.releaseReady("A");
    check(store.path(ready).empty() && !store.path(pending).empty(),
          "releaseReady keeps the clips still being filled");
    store.forget("A");
    check(store.path(pending).empty(), "forget releases every clip of the app");
    std::ofstream(store.path(store.reserve("C", 10))) << "x";
  }
  check(!exists(folder), "the store removes its folder with the files in it");
}

void staleFolders(const std::string& parent) {
  const pid_t gone = awtrix::test::endedProcess();
  const std::string stale = parent + "/awtrix-clips-" + std::to_string(gone) + "-a1B2c3";
  require(::mkdir(stale.c_str(), 0700) == 0, "the folder of a crashed store");
  std::ofstream(stale + "/1", std::ios::binary) << "x";
  std::ofstream(stale + "/2", std::ios::binary) << "y";
  ClipStore store(parent, available(kPlenty));
  check(!exists(stale), "a new store removes the folder a crashed one left behind");
  const std::string own = parent + "/awtrix-clips-" + std::to_string(::getpid()) + "-";
  check(store.ready() && store.path(store.reserve("A", 10)).rfind(own, 0) == 0, "and makes its own beside it");
}

}

int main() {
  char folder[] = "/tmp/clip-store-test-XXXXXX";
  require(::mkdtemp(folder) != nullptr, "a test folder");
  wavFile(folder);
  limits(folder);
  memory(folder);
  commitAndRelease(folder);
  staleFolders(folder);
  ::rmdir(folder);
  return awtrix::test::finish("tc002-clip-store");
}
