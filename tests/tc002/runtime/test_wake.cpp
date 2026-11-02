#include "../../support.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "platform/tc002/wake/ModelStream.h"
#include "platform/tc002/wake/WakeDetector.h"
#include "platform/tc002/wake/WakeFrontend.h"
#include "platform/tc002/wake/WakeModel.h"
#include "platform/tc002/wake/WakeSensitivity.h"

using namespace awtrix::tc002::wake;

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
    out.push_back(model.invoke());
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
    invokes += expected.size();
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
  std::printf("%s: %zu invokes bit-exact (%zu wrong), stride %zu\n", name.c_str(), invokes, wrong, model->frames());
}

uint64_t invokePosition(std::size_t invoke, std::size_t stride) {
  return WakeFrontend::kWindowSamples + WakeFrontend::kStepSamples * ((invoke + 1) * stride - 1);
}

void testStream(const std::string& reference, const std::vector<uint8_t>& file, const std::vector<int16_t>& pcm) {
  std::string error;
  ModelStream stream;
  const int16_t* samples = pcm.data();
  std::size_t count = pcm.size();
  uint8_t probability = 0;
  check(!stream.next(samples, count, probability) && count == pcm.size(), "nothing happens without a model");
  const std::string_view bytes(reinterpret_cast<const char*>(file.data()), file.size());
  require(stream.load(bytes, error), "stream: " + error);
  const std::vector<uint8_t> expected = readFile(reference);
  const std::size_t stride = stream.model()->frames();
  for (int pass = 0; pass < 2; ++pass) {
    stream.reset();
    std::vector<uint8_t> got;
    std::size_t misplaced = 0;
    for (std::size_t at = 0; at + WakeFrontend::kStepSamples <= pcm.size(); at += WakeFrontend::kStepSamples) {
      samples = pcm.data() + at;
      count = WakeFrontend::kStepSamples;
      while (stream.next(samples, count, probability)) {
        misplaced += stream.position() != invokePosition(got.size(), stride);
        got.push_back(probability);
      }
    }
    check(got == expected, "stream probabilities differ from the reference");
    check(misplaced == 0, "an invoke happens after the last sample of its stride");
  }
}

void testDetector(const std::string& reference, const std::vector<uint8_t>& file, const std::vector<int16_t>& pcm) {
  std::string error;
  WakeDetector detector;
  check(!detector.feed(pcm.data(), pcm.size()), "nothing is heard without a model");
  const std::string_view bytes(reinterpret_cast<const char*>(file.data()), file.size());
  require(detector.load(bytes, error), "detector: " + error);
  const WakeConfig config{0.5f, 3, 0};
  require(detector.configure(config, error), error);
  ModelStream stream;
  require(stream.load(bytes, error), "stream: " + error);
  WakeDetector decision;
  require(decision.configure(config, error), error);
  const std::vector<uint8_t> expected = readFile(reference);
  std::vector<uint64_t> want;
  for (std::size_t i = 0; i < expected.size(); ++i)
    if (const auto event = decision.decide(expected[i], invokePosition(i, stream.model()->frames())))
      want.push_back(event->sample);
  const auto hear = [&] {
    std::vector<uint64_t> heard;
    for (std::size_t at = 0; at + WakeFrontend::kStepSamples <= pcm.size(); at += WakeFrontend::kStepSamples)
      if (const auto detection = detector.feed(pcm.data() + at, WakeFrontend::kStepSamples))
        heard.push_back(detection->sample);
    return heard;
  };
  check(hear() == want, "the detector hears where the window over the reference fires");
  std::printf("detector: %zu detections over the reference signal\n", want.size());
  check(!detector.configure({0.5f, 0}, error) && error == "the window must be within 1..64" &&
            !detector.configure({1.5f, 3}, error) && error == "the cutoff must be within 0..1" &&
            detector.configure({0.5f, 64}, error),
        "bad windows and cutoffs are refused: " + error);
  const std::string garbage(64, 'x');
  check(!detector.load(garbage, error) && !error.empty(), "a bad file is refused");
  require(detector.configure(config, error), error);
  detector.reset();
  check(hear() == want, "a bad file keeps the model");
}

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
        WakeDetector decision;
        std::string error;
        require(decision.configure({cutoff, window, cooloffMs}, error), error);
        std::vector<std::size_t> mine;
        for (std::size_t i = 0; i < raw.size(); ++i) {
          const uint64_t sample = 480 + 160 * static_cast<uint64_t>(i);
          if (const auto event = decision.decide(raw[i], sample)) {
            mine.push_back(i);
            check(event->sample == sample && event->score >= cutoff, "event fields");
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
  WakeDetector decision;
  std::string error;
  require(decision.configure({0.6f, 4, 1000}, error), error);
  for (int i = 0; i < 3; ++i) check(!decision.decide(255, 1000 + i), "no detection before the window is full");
  const auto full = decision.decide(255, 2000);
  check(full && full->score == 1.0f, "full window fires");
  check(!decision.configure({-0.1f, 3, 0}, error) && !decision.configure({0.5f, 65, 0}, error),
        "bad configs are refused");
  require(decision.configure({0.6f, 2, 1000, 1000}, error), error);
  bool early = false;
  for (uint64_t sample = 480; sample < 16000; sample += 480) early |= decision.decide(255, sample).has_value();
  check(!early, "nothing fires within the warm-up");
  const auto first = decision.decide(255, 16000);
  check(first && first->sample == 16000, "the first output after the warm-up fires");
  std::printf("window: %zu configurations match the trainer's streaming detections\n", cases);
}

bool firesOnSteady(uint8_t probability, WakeSensitivity level) {
  WakeDetector detector;
  std::string error;
  require(detector.configure({0.86f, 3, 0}, error), error);
  detector.setSensitivity(level);
  bool fired = false;
  for (uint64_t sample = 480; sample <= 1920; sample += 480) fired |= detector.decide(probability, sample).has_value();
  return fired;
}

std::size_t detections(const std::vector<uint8_t>& probabilities, WakeSensitivity level) {
  WakeDetector detector;
  std::string error;
  require(detector.configure({0.86f, 3, 0}, error), error);
  detector.setSensitivity(level);
  std::size_t count = 0;
  for (std::size_t i = 0; i < probabilities.size(); ++i)
    count += detector.decide(probabilities[i], (i + 1) * 480).has_value();
  return count;
}

void testSensitivity(const std::string& reference) {
  check(wakeCutoff(0.86f, WakeSensitivity::Normal) == 0.86f, "normal is the model's own value");
  check(std::fabs(wakeCutoff(0.86f, WakeSensitivity::Low) - 0.92f) < 0.001f, "low needs 0.92 of the shipped model");
  check(std::fabs(wakeCutoff(0.86f, WakeSensitivity::High) - 0.75f) < 0.001f, "high needs 0.75 of the shipped model");
  const float low = wakeCutoff(0.97f, WakeSensitivity::Low), high = wakeCutoff(0.97f, WakeSensitivity::High);
  check(low > 0.97f && low < 1.0f && high < 0.97f && high > 0.9f, "a replaced model keeps three usable levels");
  check(wakeCutoff(1.0f, WakeSensitivity::High) == 1.0f && wakeCutoff(0.0f, WakeSensitivity::High) == 0.0f,
        "the levels stay within 0..1");

  struct Case {
    uint8_t probability;
    bool low, normal, high;
  };
  for (const Case& c : {Case{179, false, false, false}, Case{204, false, false, true}, Case{230, false, true, true},
                        Case{243, true, true, true}}) {
    const std::string at = std::to_string(c.probability);
    check(firesOnSteady(c.probability, WakeSensitivity::Low) == c.low, "low at " + at);
    check(firesOnSteady(c.probability, WakeSensitivity::Normal) == c.normal, "normal at " + at);
    check(firesOnSteady(c.probability, WakeSensitivity::High) == c.high, "high at " + at);
  }

  WakeDetector detector;
  std::string error;
  require(detector.configure({0.86f, 3, 0}, error), error);
  uint64_t sample = 480;
  for (int i = 0; i < 5; ++i, sample += 480) check(!detector.decide(204, sample), "normal stays silent at 0.80");
  detector.setSensitivity(WakeSensitivity::High);
  check(detector.decide(204, sample).has_value(), "a new level counts at once, on the same window");
  sample += 480;
  detector.setSensitivity(WakeSensitivity::Normal);
  check(!detector.decide(204, sample), "and the old one comes back the same way");

  const std::vector<uint8_t> probabilities = readFile(reference);
  const std::size_t quiet = detections(probabilities, WakeSensitivity::Low),
                    usual = detections(probabilities, WakeSensitivity::Normal),
                    eager = detections(probabilities, WakeSensitivity::High);
  check(quiet <= usual && usual <= eager, "the reference signal fires least on low and most on high");
  std::printf("sensitivity: %zu low, %zu normal, %zu high over the reference signal\n", quiet, usual, eager);

  WakeSensitivity parsed = WakeSensitivity::Normal;
  check(parseWakeSensitivity("low", parsed) && parsed == WakeSensitivity::Low && wakeSensitivityName(parsed) == "low",
        "low reads and writes");
  check(parseWakeSensitivity("high", parsed) && parsed == WakeSensitivity::High && wakeSensitivityName(parsed) == "high",
        "high reads and writes");
  check(parseWakeSensitivity("normal", parsed) && parsed == WakeSensitivity::Normal &&
            wakeSensitivityName(parsed) == "normal",
        "normal reads and writes");
  check(!parseWakeSensitivity("loud", parsed) && parsed == WakeSensitivity::Normal, "other words are refused");
}

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
  require(WakeModel::load(good.data(), good.size(), error) != nullptr, "the model loads: " + error);

  std::size_t truncated = 0, tried = 0;
  for (std::size_t size = 0; size < good.size(); size += size < good.size() - 512 ? 13 : 1) {
    truncated += !WakeModel::load(good.data(), size, error);
    ++tried;
  }
  check(truncated == tried, "truncated files are refused");
  std::vector<uint8_t> bad = good;
  for (int i = 0; i < 4; ++i) bad[i] = 0xFF;
  check(!WakeModel::load(bad.data(), bad.size(), error), "a root offset past the end is refused");

  for (int code : {0, 6, 17, 18, 28, 32, 40}) {
    bad = good;
    Patch patch{bad};
    for (std::size_t i = 0; i < 3; ++i) {
      const std::size_t table = patch.opcode(i);
      if (const std::size_t deprecated = patch.field(table, 0)) bad[deprecated] = static_cast<uint8_t>(code);
      if (const std::size_t builtin = patch.field(table, 3)) std::memcpy(&bad[builtin], &code, 4);
    }
    const bool refused = !WakeModel::load(bad.data(), bad.size(), error);
    check(refused && error.find("unsupported operator " + std::to_string(code)) == 0,
          "unsupported op: got '" + error + "'");
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
  require(argc >= 5 && (argc - 2) % 3 == 0, "usage: tc002-wake-test <fixtures> <name> <model> <golden dir>...");
  const std::string dir = argv[1];
  const std::vector<int16_t> pcm = synthetic(4);

  testFrontend(dir, pcm);
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
  const std::vector<uint8_t> model = readFile(argv[3]);
  const std::string reference = std::string(argv[4]) + "/" + argv[2] + ".signal.bin";
  testStream(reference, model, pcm);
  testDetector(reference, model, pcm);
  testWindow();
  testSensitivity(reference);
  testRefusals(model);
  return awtrix::test::finish("tc002-wake");
}
