#include "../../support.h"
// The wake-word engine against the references of the microWakeWord trainer, as
// tools/tc002/wake/make_golden.py recorded them in tests/tc002/runtime/wake: the frontend against
// pymicro_features, the smoke model and an op coverage model (tools/tc002/wake/zoo_model.py) against
// LiteRT's reference kernels, the detector end to end,
// the decision against the trainer's streaming benchmark, and the loader's refusals.
//
// tc002-wake-test <fixtures> [<name> <model.tflite> <golden dir>]... also checks further models
// against a golden dir of make_golden.py: its signal and clip<k> features, <name>.signal.bin,
// .random.bin and .clip<k>.bin.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/wake/WakeDetector.h"
#include "platform/tc002/wake/WakeFrontend.h"
#include "platform/tc002/wake/WakeModel.h"

using namespace awtrix::tc002::wake;
using Clock = std::chrono::steady_clock;

namespace {

using awtrix::test::check;
using awtrix::test::require;

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::vector<uint16_t> readU16(const std::string& path) {
  const std::vector<uint8_t> bytes = readFile(path);
  std::vector<uint16_t> values(bytes.size() / 2);
  for (std::size_t i = 0; i < values.size(); ++i) values[i] = uint16_t(bytes[2 * i] | bytes[2 * i + 1] << 8);
  return values;
}

std::vector<int16_t> readPcm(const std::string& path) {
  const std::vector<uint16_t> raw = readU16(path);
  return std::vector<int16_t>(raw.begin(), raw.end());
}

struct XorShift {
  uint32_t state;
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
};

// synthetic() of make_golden.py.
std::vector<int16_t> synthetic(int seconds) {
  constexpr int rate = 16000;
  XorShift rng{0x12345678u};
  std::vector<int16_t> out(static_cast<std::size_t>(seconds * rate));
  uint32_t phase = 0;
  const int chirpStart = 3 * rate / 2, chirpSamples = 3 * rate / 2;
  const uint32_t inc0 = static_cast<uint32_t>(100 * (uint64_t{1} << 32) / rate);
  const uint32_t slope = static_cast<uint32_t>((7000 * (uint64_t{1} << 32) / rate - inc0) / chirpSamples);
  for (std::size_t n = 0; n < out.size(); ++n) {
    const int32_t noise = static_cast<int32_t>(rng.next() >> 16) - 32768;
    const int i = static_cast<int>(n % (4 * rate));
    int32_t value = 0;
    if (i < rate / 2) {
      value = 0;
    } else if (i < chirpStart) {
      value = (noise * ((i - rate / 2) * 2048 / rate)) >> 12;
    } else if (i < chirpStart + chirpSamples) {
      phase += inc0 + static_cast<uint32_t>(i - chirpStart) * slope;
      const int32_t p = static_cast<int32_t>(phase >> 16);
      const int32_t half = p & 32767;
      const int32_t tone = (((half * (32768 - half)) >> 13) * 12000) >> 15;
      value = (p < 32768 ? tone : -tone) + (noise >> 6);
    } else if (i < 3 * rate + 2 * rate / 5) {
      value = 0;
    } else {
      value = (i / 1600) % 2 == 0 ? ((i / 40) % 2 ? 32767 : -32768) : noise >> 4;
    }
    out[n] = static_cast<int16_t>(value);
  }
  return out;
}

// random_frames() of make_golden.py.
std::vector<int8_t> randomFrames(std::size_t frames) {
  XorShift rng{0x9E3779B9u};
  std::vector<int8_t> out(frames * WakeModel::kFeatures);
  for (int8_t& v : out) v = static_cast<int8_t>(static_cast<int32_t>((rng.next() >> 24) ^ 0x80) - 128);
  return out;
}

std::vector<uint16_t> features(const std::vector<int16_t>& pcm, std::size_t block) {
  WakeFrontend frontend;
  std::vector<uint16_t> out;
  WakeFrontend::Frame frame;
  for (std::size_t at = 0; at < pcm.size(); at += block) {
    const int16_t* p = pcm.data() + at;
    std::size_t n = std::min(block, pcm.size() - at);
    while (frontend.next(p, n, frame)) out.insert(out.end(), frame.begin(), frame.end());
  }
  return out;
}

std::size_t mismatches(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  std::size_t count = std::max(a.size(), b.size()) - std::min(a.size(), b.size());
  for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) count += a[i] != b[i];
  return count;
}

void testFrontend(const std::string& dir, const std::vector<int16_t>& pcm) {
  const std::vector<uint16_t> expected = readU16(dir + "/signal.features.bin");
  require(expected.size() == 398 * WakeModel::kFeatures, "signal.features");
  for (std::size_t block : {std::size_t{1}, std::size_t{7}, std::size_t{160}, std::size_t{480},
                            std::size_t{1001}, pcm.size()}) {
    const std::vector<uint16_t> got = features(pcm, block);
    std::size_t wrong = got.size() == expected.size() ? 0 : expected.size();
    for (std::size_t i = 0; !wrong && i < got.size(); ++i) wrong += got[i] != expected[i];
    check(wrong == 0, "frontend frames differ from pymicro_features, block " + std::to_string(block));
  }
  WakeFrontend frontend;
  WakeFrontend::Frame frame;
  const int16_t* p = pcm.data();
  std::size_t n = pcm.size() / 2;
  while (frontend.next(p, n, frame)) {
  }
  frontend.reset();
  p = pcm.data();
  n = pcm.size();
  std::vector<uint16_t> again;
  while (frontend.next(p, n, frame)) again.insert(again.end(), frame.begin(), frame.end());
  check(again == expected, "frontend reset");
  std::printf("frontend: %zu frames bit-exact with pymicro_features\n",
              expected.size() / WakeModel::kFeatures);
}

std::vector<int8_t> trainerInput(const std::vector<uint16_t>& values, const WakeModel& model) {
  std::vector<int8_t> out(values.size());
  for (std::size_t i = 0; i < values.size(); ++i) {
    const float q = std::nearbyint(static_cast<float>(trainerFeature(values[i])) / model.inputScale()) +
                    static_cast<float>(model.inputZeroPoint());
    out[i] = static_cast<int8_t>(std::max(-128.0f, std::min(127.0f, q)));
  }
  return out;
}

std::vector<uint8_t> runModel(WakeModel& model, const std::vector<int8_t>& frames) {
  std::vector<uint8_t> out;
  const std::size_t stride = model.frames() * WakeModel::kFeatures;
  model.reset();
  for (std::size_t at = 0; at + stride <= frames.size(); at += stride) {
    std::memcpy(model.input(), frames.data() + at, stride);
    model.invoke();
    out.insert(out.end(), model.output(), model.output() + model.outputSize());
  }
  return out;
}

void checkModel(const std::string& name, const std::vector<uint8_t>& file, const std::string& golden,
                const std::vector<uint16_t>& signal) {
  std::string error;
  std::unique_ptr<WakeModel> model = WakeModel::load(file.data(), file.size(), error);
  require(model != nullptr, name + ": " + error);
  std::size_t invokes = 0, wrong = 0;
  auto compare = [&](const char* stream, const std::vector<int8_t>& frames) {
    const std::vector<uint8_t> expected = readFile(golden + "/" + name + "." + stream + ".bin");
    require(!expected.empty(), golden + "/" + name + "." + stream + ".bin");
    const std::vector<uint8_t> got = runModel(*model, frames);
    const std::size_t bad = mismatches(got, expected);
    check(bad == 0, name + ": " + stream + " outputs differ from the reference in " + std::to_string(bad));
    invokes += expected.size() / model->outputSize();
    wrong += bad;
  };
  const std::vector<int8_t> signalInput = trainerInput(signal, *model);
  compare("signal", signalInput);
  compare("random", randomFrames(800));
  compare("signal", signalInput);
  for (int k = 0; std::ifstream(golden + "/clip" + std::to_string(k) + ".pcm"); ++k) {
    const std::string clip = golden + "/clip" + std::to_string(k);
    const std::vector<uint16_t> values = features(readPcm(clip + ".pcm"), 480);
    check(values == readU16(clip + ".features.bin"), clip + ": frontend frames differ from pymicro_features");
    compare(("clip" + std::to_string(k)).c_str(), trainerInput(values, *model));
  }

  const std::vector<int8_t> timing = randomFrames(800);
  const auto start = Clock::now();
  const std::vector<uint8_t> once = runModel(*model, timing);
  const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() /
                    static_cast<double>(once.size() / model->outputSize());
  std::printf("%s: %zu invokes bit-exact (%zu wrong), stride %zu, arena %zu bytes, %.1f us per invoke, "
              "%.1f us per 10 ms\n", name.c_str(), invokes, wrong, model->frames(), model->arenaBytes(), us,
              us / static_cast<double>(model->frames()));
}

void testDetector(const std::string& dir, const std::vector<uint8_t>& file, const std::vector<int16_t>& pcm) {
  std::string error;
  WakeDetector detector;
  check(!detector.feed(pcm.data(), pcm.size()) && detector.position() == 0,
        "nothing happens without a model");
  const std::string_view bytes(reinterpret_cast<const char*>(file.data()), file.size());
  require(detector.load(bytes, error), "detector: " + error);
  require(detector.configure(0.5f, 3), "configure");
  const std::vector<uint8_t> expected = readFile(dir + "/smoke.signal.bin");
  const std::size_t stride = detector.model()->frames();
  for (int pass = 0; pass < 2; ++pass) {
    detector.reset();
    std::size_t invokes = 0, wrong = 0;
    for (std::size_t at = 0; at + 160 <= pcm.size(); at += 160) {
      detector.feed(pcm.data() + at, 160);
      const uint64_t position = detector.position();
      if (position < 480 || ((position - 480) / 160 + 1) % stride != 0) continue;
      wrong += invokes >= expected.size() || detector.probability() != expected[invokes] / 255.0f;
      ++invokes;
    }
    check(invokes == expected.size() && wrong == 0, "detector probabilities differ from the reference");
  }
  check(!detector.configure(0.5f, 0) && !detector.configure(1.5f, 3) && detector.configure(0.5f, 64),
        "bad windows and cutoffs are refused");
  const std::string garbage(64, 'x');
  check(!detector.load(garbage, error) && !error.empty() && detector.loaded(), "a bad file keeps the model");

  WakeFrontend frontend;
  std::vector<uint16_t> sink;
  const auto start = Clock::now();
  for (int i = 0; i < 4; ++i) sink = features(pcm, 480);
  const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / (4 * 398);
  std::printf("frontend: %.1f us per 10 ms frame\n", us);
}

// eval/streaming.py detections(), with probabilities raw / 255.
std::vector<std::size_t> trainerDetections(const std::vector<uint8_t>& raw, double cutoff, std::size_t window,
                                           long cooloffFrames) {
  std::vector<std::size_t> out;
  long last = -1000000000;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (i + 1 < window) continue;
    double sum = 0;
    for (std::size_t k = i + 1 - window; k <= i; ++k) sum += raw[k] / 255.0;
    if (sum / static_cast<double>(window) >= cutoff && static_cast<long>(i) - last > cooloffFrames) {
      out.push_back(i);
      last = static_cast<long>(i);
    }
  }
  return out;
}

void testWindow() {
  XorShift rng{0xC0FFEEu};
  std::size_t cases = 0;
  for (std::size_t window : {1, 3, 5, 10, 64}) {
    for (float cutoff : {0.55f, 0.8f, 0.97f, 0.99f, 1.0f}) {
      for (uint32_t cooloffMs : {0u, 100u, 1000u, 2500u}) {
        std::vector<uint8_t> raw(3000);
        for (std::size_t i = 0; i < raw.size(); ++i) {
          const uint32_t r = rng.next();
          raw[i] = (i / 150) % 3 == 1 ? static_cast<uint8_t>(240 + (r >> 28)) : static_cast<uint8_t>(r >> 25);
          if ((r & 0xFF) == 7) raw[i] = 255;
        }
        WakeWindow decision;
        std::string error;
        require(decision.configure({cutoff, window, cooloffMs}, error), error);
        std::vector<std::size_t> mine;
        WakeEvent event;
        for (std::size_t i = 0; i < raw.size(); ++i) {
          const uint64_t sample = 480 + 160 * static_cast<uint64_t>(i);
          if (decision.push(raw[i], sample, event)) {
            mine.push_back(i);
            check(event.sample == sample && event.peak * 255.0f + 0.5f >= raw[i], "event fields");
          }
        }
        const auto expected =
          trainerDetections(raw, static_cast<double>(cutoff), window, std::lround(cooloffMs / 10.0));
        check(mine == expected, "window " + std::to_string(window) + " cutoff " + std::to_string(cutoff) +
                                  " cool-off " + std::to_string(cooloffMs) + ": detections differ");
        ++cases;
      }
    }
  }
  WakeWindow decision;
  std::string error;
  require(decision.configure({0.6f, 4, 1000}, error), error);
  WakeEvent event;
  for (int i = 0; i < 3; ++i) {
    check(!decision.push(255, 1000 + i, event), "no detection before the window is full");
    check(decision.mean() == 0.0f, "no mean before the window is full");
  }
  check(decision.push(255, 2000, event) && event.score == 1.0f && event.peak == 1.0f, "full window fires");
  check(decision.takePeak() == 1.0f && decision.takePeak() == 0.0f, "takePeak resets");
  check(!decision.configure({-0.1f, 3, 0}, error) && !decision.configure({0.5f, 65, 0}, error),
        "bad configs are refused");
  require(decision.configure({0.6f, 2, 1000, 1000}, error), error);
  bool early = false;
  for (uint64_t sample = 480; sample < 16000; sample += 480) early |= decision.push(255, sample, event);
  check(!early && decision.takePeak() == 0.0f, "nothing fires and no peak counts within the warm-up");
  check(decision.push(255, 16000, event) && event.sample == 16000, "the first output after the warm-up fires");
  std::printf("window: %zu configurations match the trainer's streaming detections\n", cases);
}

// The few flatbuffer steps the refusal tests need to find a field to break.
struct Patch {
  std::vector<uint8_t>& b;
  uint32_t u32(std::size_t at) const {
    return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | uint32_t(b[at + 3]) << 24;
  }
  std::size_t field(std::size_t table, int id) const {
    const std::size_t vtable = table - static_cast<int32_t>(u32(table));
    const uint16_t size = uint16_t(b[vtable] | b[vtable + 1] << 8);
    if (4 + 2u * id + 2 > size) return 0;
    const uint16_t offset = uint16_t(b[vtable + 4 + 2 * id] | b[vtable + 5 + 2 * id] << 8);
    return offset ? table + offset : 0;
  }
  std::size_t follow(std::size_t at) const { return at + u32(at); }
  std::size_t opcode(std::size_t i) const {
    const std::size_t codes = follow(field(u32(0), 1));
    return follow(codes + 4 + 4 * i);
  }
};

void testRefusals(const std::vector<uint8_t>& good) {
  std::string error;
  check(!WakeModel::load(nullptr, 0, error) && !error.empty(), "no data is refused");
  const std::vector<uint8_t> garbage(4096, 0xA5);
  check(!WakeModel::load(garbage.data(), garbage.size(), error), "garbage is refused");
  require(WakeModel::load(good.data(), good.size(), error) != nullptr, "the smoke model loads: " + error);

  std::size_t truncated = 0, tried = 0;
  for (std::size_t size = 0; size < good.size(); size += size < good.size() - 512 ? 13 : 1) {
    truncated += !WakeModel::load(good.data(), size, error);
    ++tried;
  }
  check(truncated == tried, "truncated files are refused");
  std::vector<uint8_t> bad = good;
  for (int i = 0; i < 4; ++i) bad[i] = 0xFF;
  check(!WakeModel::load(bad.data(), bad.size(), error), "a root offset past the end is refused");

  for (int code : {28, 32, 6}) {
    bad = good;
    Patch patch{bad};
    for (std::size_t i = 0; i < 3; ++i) {
      const std::size_t table = patch.opcode(i);
      if (const std::size_t deprecated = patch.field(table, 0)) bad[deprecated] = static_cast<uint8_t>(code);
      if (const std::size_t builtin = patch.field(table, 3)) std::memcpy(&bad[builtin], &code, 4);
    }
    const bool refused = !WakeModel::load(bad.data(), bad.size(), error);
    const std::string want = code == 28  ? "unsupported operator TANH"
                             : code == 6 ? "unsupported operator DEQUANTIZE"
                                         : "unsupported custom operator";
    check(refused && error.find(want) == 0, "unsupported op: got '" + error + "'");
  }

  XorShift rng{0xBADC0DEu};
  std::size_t loaded = 0;
  for (int i = 0; i < 3000; ++i) {
    bad = good;
    const int flips = 1 + static_cast<int>(rng.next() % 4);
    for (int f = 0; f < flips; ++f) bad[rng.next() % bad.size()] = static_cast<uint8_t>(rng.next());
    std::unique_ptr<WakeModel> model = WakeModel::load(bad.data(), bad.size(), error);
    if (!model) {
      check(!error.empty(), "a refusal has a reason");
      continue;
    }
    ++loaded;
    for (int k = 0; k < 3; ++k) model->invoke();
  }
  std::printf("refusals: %zu of %zu truncations refused, %zu of 3000 mutated files still loaded and ran\n",
              truncated, tried, loaded);
}

}

int main(int argc, char** argv) {
  require(argc >= 2, "usage: tc002-wake-test <fixtures> [<name> <model> <golden dir>]...");
  const std::string dir = argv[1];
  const std::vector<int16_t> pcm = synthetic(4);
  const std::vector<uint8_t> smoke = readFile(dir + "/smoke.tflite");
  require(!smoke.empty(), dir + "/smoke.tflite");

  testFrontend(dir, pcm);
  const std::vector<uint16_t> signal = readU16(dir + "/signal.features.bin");
  checkModel("smoke", smoke, dir, signal);
  checkModel("zoo", readFile(dir + "/zoo.tflite"), dir, signal);
  for (int i = 2; i + 2 < argc; i += 3) {
    const std::string golden = argv[i + 2];
    const std::vector<uint16_t> values = readU16(golden + "/signal.features.bin");
    const std::size_t frames = values.size() / WakeModel::kFeatures;
    const int seconds = static_cast<int>(((frames - 1) * 160 + 480 + 15999) / 16000);
    check(features(synthetic(seconds), 4000) == values,
          golden + ": frontend frames differ from pymicro_features");
    std::printf("%s: %zu signal frames checked against pymicro_features\n", golden.c_str(), frames);
    checkModel(argv[i], readFile(argv[i + 1]), golden, values);
  }
  testDetector(dir, smoke, pcm);
  testWindow();
  testRefusals(smoke);
  return awtrix::test::finish("tc002-wake");
}
