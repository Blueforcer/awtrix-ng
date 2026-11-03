#include "../../support.h"
// The clock's voice as files: a pack is read in place from the release or from an upload, and a
// voice that is still speaking outlives the file it came from.
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/tc002/speech/SpeechTypes.h"
#include "platform/tc002/speech/VoicePacks.h"

using namespace awtrix::speech;
using awtrix::test::check;
namespace fs = std::filesystem;

namespace {

std::string readBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeBytes(const std::string& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Whether the address lies in a mapping of the file, per /proc/self/maps.
bool mappedFrom(const void* address, const std::string& path) {
  std::ifstream maps("/proc/self/maps");
  const auto at = reinterpret_cast<uintptr_t>(address);
  for (std::string line; std::getline(maps, line);) {
    if (line.find(path) == std::string::npos) continue;
    unsigned long low = 0, high = 0;
    char dash = 0;
    std::istringstream(line) >> std::hex >> low >> dash >> high;
    if (at >= low && at < high) return true;
  }
  return false;
}

std::size_t mappingsOf(const std::string& path) {
  std::ifstream maps("/proc/self/maps");
  std::size_t count = 0;
  for (std::string line; std::getline(maps, line);)
    if (line.find(path) != std::string::npos) ++count;
  return count;
}

// A model that goes lets go of its file on another thread.
bool noMappingOf(const std::string& path) {
  for (int i = 0; i < 500 && mappingsOf(path); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  return mappingsOf(path) == 0;
}

std::vector<int16_t> speak(SpeechUtterance& utterance, std::size_t limit = SIZE_MAX) {
  std::vector<int16_t> out;
  int16_t block[512];
  while (out.size() < limit) {
    const std::size_t frames = utterance.render(block, 512);
    if (!frames) break;
    out.insert(out.end(), block, block + frames);
  }
  return out;
}

class Rig {
 public:
  explicit Rig(const std::string& builtin)
      : folder_(fs::temp_directory_path() / ("voice-packs-" + std::to_string(::getpid()))),
        user_((folder_ / "speech" / "voice.atts").string()),
        builtin_((folder_ / "builtin.atts").string()),
        packs_(builtin_, user_, 0, [this](std::shared_ptr<SpeechVoice> voice) {
          applied_ = std::dynamic_pointer_cast<ModelVoice>(std::move(voice));
        }) {
    fs::remove_all(folder_);
    fs::create_directories(folder_);
    writeBytes(builtin_, builtin);
  }
  ~Rig() { fs::remove_all(folder_); }

  httplib::Response call(const std::string& method, const std::string& body = "") {
    httplib::Request request;
    request.method = method;
    request.path = "/api/v1/speech/voice";
    request.body = body;
    httplib::Response response;
    check(packs_.handle(request, response), "the route is the pack's");
    return response;
  }

  VoicePacks& packs() { return packs_; }
  const std::shared_ptr<ModelVoice>& voice() const { return applied_; }
  const std::string& user() const { return user_; }
  const std::string& builtin() const { return builtin_; }

 private:
  fs::path folder_;
  std::string user_, builtin_;
  std::shared_ptr<ModelVoice> applied_;
  VoicePacks packs_;
};

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void testBuiltin(const std::string& pack) {
  Rig rig(pack);
  check(rig.packs().load() && rig.voice(), "the release's pack loads");
  if (!rig.voice()) return;
  check(mappedFrom(rig.voice()->model().phones, rig.builtin()), "its weights are read from the file, no copy");
  check(reinterpret_cast<uintptr_t>(rig.voice()->model().phones) % 16 == 0, "at the alignment the kernels expect");
  check(has(rig.call("GET").body, "\"uploaded\":false"), "and it is not an upload");
}

void testUpload(const std::string& pack) {
  Rig rig(pack);
  rig.packs().load();
  const httplib::Response installed = rig.call("POST", pack);
  check(installed.status == -1 || installed.status == 200, "an upload is accepted");
  check(has(installed.body, "\"uploaded\":true"), "and reported: " + installed.body);
  check(readBytes(rig.user()) == pack, "and stored");
  check(rig.voice() && mappedFrom(rig.voice()->model().phones, rig.user()), "it speaks from flash, not from the upload");

  std::string flipped = pack;
  flipped[flipped.size() / 2] ^= 1;
  const auto before = rig.voice();
  const httplib::Response refused = rig.call("POST", flipped);
  check(refused.status == 400 && has(refused.body, "checksum mismatch"), "a damaged upload is refused: " + refused.body);
  check(rig.voice() == before && readBytes(rig.user()) == pack, "and changes nothing");
  check(rig.call("POST", "").status == 400, "so is an empty one");
}

// Replacing or removing a pack while an utterance reads it: the utterance goes on from the old file,
// which disappears with the last user.
void testReplacedWhileSpeaking(const std::string& pack) {
  Rig rig(pack);
  rig.packs().load();
  rig.call("POST", pack);
  std::shared_ptr<ModelVoice> old = rig.voice();
  if (!old) return check(false, "the first upload");
  Plan plan;
  check(old->prepare("Hello, world.", plan), "a plan");
  auto reference = old->start(plan);
  const std::vector<int16_t> whole = speak(*reference);
  check(!whole.empty(), "the voice speaks");
  reference.reset();

  auto spoken = old->start(plan);
  std::vector<int16_t> heard = speak(*spoken, whole.size() / 2);
  rig.call("POST", pack);
  check(rig.voice() != old && mappedFrom(rig.voice()->model().phones, rig.user()), "the new pack is in place");
  check(mappingsOf(rig.user()) == 2, "next to the old file, still mapped");
  const std::vector<int16_t> rest = speak(*spoken);
  heard.insert(heard.end(), rest.begin(), rest.end());
  check(heard == whole, "the utterance under way ends word for word");
  spoken.reset();

  auto after = rig.voice()->start(plan);
  check(speak(*after) == whole, "the new voice speaks from the next speech on");
  after.reset();

  spoken = old->start(plan);
  heard = speak(*spoken, whole.size() / 2);
  const httplib::Response removed = rig.call("DELETE");
  check(has(removed.body, "\"uploaded\":false") && !fs::exists(rig.user()), "removing it goes back to the release's");
  check(mappedFrom(rig.voice()->model().phones, rig.builtin()), "which is in place");
  const std::vector<int16_t> end = speak(*spoken);
  heard.insert(heard.end(), end.begin(), end.end());
  check(heard == whole, "an utterance under way outlives the removed file");
  spoken.reset();
  old.reset();
  check(noMappingOf(rig.user()), "and the last user lets go of the old files");
}

std::size_t lockedKb() {
  std::ifstream status("/proc/self/status");
  for (std::string line; std::getline(status, line);)
    if (line.rfind("VmLck:", 0) == 0) return std::strtoul(line.c_str() + 6, nullptr, 10);
  return 0;
}

// An utterance holds the weights in memory while it speaks, however many speak, and only the weights.
void testHeldWhileSpeaking(const std::string& pack, const std::string& shippedPack) {
  Rig rig(pack);
  rig.packs().load();
  const std::shared_ptr<ModelVoice> voice = rig.voice();
  if (!voice) return check(false, "a voice");
  Plan plan;
  check(voice->prepare("Hello, world.", plan), "a plan");
  check(lockedKb() == 0, "nothing is held before speech");
  bool lockable = false;
  {
    const SpeechModel::Hold probe(voice->model());
    lockable = lockedKb() > 0;
    if (!lockable) {
      std::puts("this user may not lock memory: only the refusal is tested");
    } else {
      auto first = voice->start(plan);
      const std::size_t held = lockedKb();
      auto second = voice->start(plan);
      check(lockedKb() == held && held <= pack.size() / 1024 + 4, "two holds lock it once");
      first.reset();
      check(lockedKb() == held, "the first to end leaves it held");
      second.reset();
      check(lockedKb() == held, "and a third hold keeps it");
    }
  }
  check(lockedKb() == 0, "the last one lets go");
  auto utterance = voice->start(plan);
  check(lockedKb() > 0 || !lockable, "an utterance holds it");
  check(!speak(*utterance, 4096).empty(), "and speaks");
  utterance.reset();
  check(lockedKb() == 0, "and lets go when it ends");

  if (lockable) {
    Rig shipped(readBytes(shippedPack));
    shipped.packs().load();
    const SpeechModel& model = shipped.voice()->model();
    const SpeechModel::Hold hold(model);
    const std::size_t weights = (readBytes(shippedPack).size() - model.frontendLexicon.size()) / 1024;
    check(!model.frontendLexicon.empty() && lockedKb() >= weights && lockedKb() <= weights + 8,
          "a hold leaves out the lexicon, which only the frontend's copy serves: " + std::to_string(lockedKb()));
  }

  const rlimit none{0, 0};
  rlimit before{};
  ::getrlimit(RLIMIT_MEMLOCK, &before);
  if (::geteuid() != 0 && ::setrlimit(RLIMIT_MEMLOCK, &none) == 0) {
    auto refused = voice->start(plan);
    check(refused && !speak(*refused).empty() && lockedKb() == 0, "where the system refuses, it speaks all the same");
  }
}

void testDamagedFileOnStart(const std::string& pack) {
  Rig rig(pack);
  fs::create_directories(fs::path(rig.user()).parent_path());
  std::string flipped = pack;
  flipped[flipped.size() / 2] ^= 1;
  writeBytes(rig.user(), flipped);
  check(rig.packs().load() && rig.voice(), "a damaged upload leaves the release's voice");
  check(rig.voice() && mappedFrom(rig.voice()->model().phones, rig.builtin()), "in place");
  check(noMappingOf(rig.user()), "and is not kept mapped");

  fs::remove(rig.builtin());
  check(!rig.packs().load() && !rig.voice(), "without any pack there is no voice");
}

}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fputs("usage: tc002-voice-packs-test PACK SHIPPED_PACK\n", stderr);
    return 2;
  }
  const std::string pack = readBytes(argv[1]);
  testBuiltin(pack);
  testUpload(pack);
  testReplacedWhileSpeaking(pack);
  testDamagedFileOnStart(pack);
  testHeldWhileSpeaking(pack, argv[2]);
  if (awtrix::test::failures()) {
    std::printf("%d failure(s)\n", awtrix::test::failures());
    return 1;
  }
  std::puts("voice packs: all passed");
  return 0;
}
