#include "../../support.h"
// The voice of an ATTS model against the model's reference runtime: a small voice with random
// weights (tests/tc002/runtime/speech, written by tools/speech/make_voice_fixture.py) and what
// tts/runtime.py made of four utterances. Also the loader's refusals, the token contract, the
// chunks, cancelling, and the real-time factor of a voice of the shipped size.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "platform/tc002/speech/SpeechKernels.h"
#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechModelVoice.h"
#include "platform/tc002/speech/SpeechSynth.h"
#include "platform/tc002/speech/SpeechTokens.h"

using namespace awtrix::speech;
using Clock = std::chrono::steady_clock;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
         static_cast<uint32_t>(p[3]) << 24;
}

void put32(uint8_t* p, uint32_t value) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& data) : data_(data) {}
  uint32_t u32() {
    const auto value = values<uint32_t>(1);
    return value.empty() ? 0 : value[0];
  }
  template <typename T>
  std::vector<T> values(std::size_t count) {
    if (!ok_ || count > (data_.size() - at_) / sizeof(T)) {
      ok_ = false;
      return {};
    }
    std::vector<T> out(count);
    std::memcpy(out.data(), data_.data() + at_, count * sizeof(T));
    at_ += count * sizeof(T);
    return out;
  }
  bool ok() const { return ok_; }

 private:
  const std::vector<uint8_t>& data_;
  std::size_t at_ = 0;
  bool ok_ = true;
};

struct ChunkReference {
  std::vector<uint16_t> codes;
  std::vector<uint8_t> durations;
  std::vector<float> pitch;
  std::size_t frames = 0;
  std::vector<float> mel;
};

struct Utterance {
  std::vector<ChunkReference> chunks;
  std::vector<int16_t> pcm;
};

std::vector<Utterance> readExpectations(const std::string& path, std::size_t mels) {
  const std::vector<uint8_t> data = readFile(path);
  Reader in(data);
  std::vector<Utterance> out(in.u32());
  for (Utterance& utterance : out) {
    utterance.chunks.resize(in.u32());
    for (ChunkReference& chunk : utterance.chunks) {
      const uint32_t tokens = in.u32();
      chunk.codes = in.values<uint16_t>(tokens);
      chunk.durations = in.values<uint8_t>(tokens);
      chunk.pitch = in.values<float>(tokens);
      chunk.frames = in.u32();
      chunk.mel = in.values<float>(chunk.frames * mels);
    }
    utterance.pcm = in.values<int16_t>(in.u32());
  }
  check(in.ok() && !out.empty(), "the expectations read");
  return out;
}

Token token(uint16_t code) {
  return {static_cast<uint8_t>(code & 63u), static_cast<uint8_t>((code >> 6) & 3u),
          static_cast<uint8_t>((code >> 8) & 1u), static_cast<uint8_t>((code >> 9) & 7u)};
}

uint16_t code(const Token& t) {
  return static_cast<uint16_t>(t.phone | t.stress << 6 | t.wordEnd << 8 | t.punctuation << 9);
}

// The plan the chunks' tokens came from: every token but BOS.
std::unique_ptr<Plan> planOf(const Utterance& utterance) {
  static const char kMarks[] = {0, ',', '.', '?', '!', ';', ':'};
  auto plan = std::make_unique<Plan>();
  for (const ChunkReference& chunk : utterance.chunks) {
    for (std::size_t i = 1; i < chunk.codes.size(); ++i) {
      const Token t = token(chunk.codes[i]);
      plan->phones[plan->count] = {static_cast<PhoneId>(t.phone),
                                   static_cast<uint8_t>(t.stress | (t.wordEnd ? WordEnd : 0))};
      plan->boundaries[plan->count++] = t.punctuation < 7 ? kMarks[t.punctuation] : 0;
    }
  }
  return plan;
}

double correlation(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
  const std::size_t n = std::min(a.size(), b.size());
  double ma = 0, mb = 0;
  for (std::size_t i = 0; i < n; ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= static_cast<double>(n);
  mb /= static_cast<double>(n);
  double ab = 0, aa = 0, bb = 0;
  for (std::size_t i = 0; i < n; ++i) {
    ab += (a[i] - ma) * (b[i] - mb);
    aa += (a[i] - ma) * (a[i] - ma);
    bb += (b[i] - mb) * (b[i] - mb);
  }
  return ab / std::sqrt(aa * bb);
}

int maxDifference(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
  int most = 0;
  for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) most = std::max(most, std::abs(a[i] - b[i]));
  return most;
}

std::vector<int16_t> renderAll(SpeechUtterance& utterance, std::size_t block = 512) {
  std::vector<int16_t> out, buffer(block);
  while (const std::size_t n = utterance.render(buffer.data(), block)) out.insert(out.end(), buffer.begin(), buffer.begin() + n);
  return out;
}

// ---- the loader ----

struct EntryRef {
  std::size_t at;  // offset of the directory entry
  uint32_t kind, rows, cols, stride, offset, bytes;
};

EntryRef entry(const std::vector<uint8_t>& file, const std::string& name) {
  const uint32_t count = u32(file.data() + 20);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* e = file.data() + 64 + 64 * i;
    if (name == reinterpret_cast<const char*>(e))
      return {static_cast<std::size_t>(e - file.data()), u32(e + 40), u32(e + 44), u32(e + 48), u32(e + 52),
              u32(e + 56), u32(e + 60)};
  }
  return {};
}

void resign(std::vector<uint8_t>& file) {
  put32(file.data() + 16, 0);
  put32(file.data() + 16, crc32(file.data(), file.size()));
}

std::string refusal(const std::vector<uint8_t>& file) {
  std::string error;
  const auto model = SpeechModel::parse(file.data(), file.size(), error);
  return model ? std::string("accepted") : error;
}

void expectRefusal(const std::vector<uint8_t>& original, const char* what, const std::string& expected,
                   const std::function<void(std::vector<uint8_t>&)>& change, bool sign = true) {
  std::vector<uint8_t> file = original;
  change(file);
  if (sign) resign(file);
  const std::string got = refusal(file);
  check(got.find(expected) != std::string::npos, std::string(what) + ": expected \"" + expected + "\", got \"" + got + "\"");
}

void testLoader(const std::string& dir, const std::vector<uint8_t>& file) {
  std::string error;
  const auto model = SpeechModel::load(dir + "/voice.atts", error);
  check(model != nullptr, "the fixture loads: " + error);
  if (model) {
    check(model->sampleRate == 24000 && model->fftSize == 1024 && model->hop == 256 && model->mels == 100,
          "the header facts");
    check(model->encoderWidth() == 32 && model->decoderWidth() == 32 && model->vocoderWidth() == 32 &&
              model->encoder.size() == 3 && model->encoder[2].dilation == 4 && model->vocoder.size() == 2,
          "the config");
  }
  check(!SpeechModel::load(dir + "/missing.atts", error) && error == "No such file or directory", "a missing file");
  check(!SpeechModel::load(dir, error) && error == "not a file", "a directory");

  expectRefusal(file, "empty", "not an ATTS file", [](auto& f) { f.clear(); }, false);
  expectRefusal(file, "magic", "not an ATTS file", [](auto& f) { f[0] = 'X'; }, false);
  expectRefusal(file, "truncated", "size mismatch", [](auto& f) { f.resize(f.size() - 16); }, false);
  expectRefusal(file, "a flipped byte", "checksum mismatch", [](auto& f) { f[f.size() / 2] ^= 1; }, false);
  expectRefusal(file, "the checksum", "checksum mismatch", [](auto& f) { f[16] ^= 0x80; }, false);
  expectRefusal(file, "version", "version 2 not supported", [](auto& f) { put32(f.data() + 4, 2); });
  expectRefusal(file, "header bytes", "bad header", [](auto& f) { put32(f.data() + 8, u32(f.data() + 8) + 16); });
  expectRefusal(file, "tensor count", "bad header", [](auto& f) { put32(f.data() + 20, 0); });
  expectRefusal(file, "sample rate", "sample rate 22050 not supported", [](auto& f) { put32(f.data() + 24, 22050); });
  expectRefusal(file, "hop", "bad STFT", [](auto& f) { put32(f.data() + 28, 0); });
  expectRefusal(file, "FFT size", "bad STFT", [](auto& f) { put32(f.data() + 32, 1000); });
  expectRefusal(file, "token contract", "token contract 2 not supported", [](auto& f) { put32(f.data() + 40, 2); });
  expectRefusal(file, "flags", "flags not supported", [](auto& f) { put32(f.data() + 44, 1); });

  const EntryRef phone = entry(file, "am.phone");
  const EntryRef pw1 = entry(file, "am.enc.0.pw1");
  const EntryRef config = entry(file, "config");
  check(phone.bytes && pw1.bytes && config.bytes, "the fixture's directory");
  expectRefusal(file, "an offset past the end", "tensor am.phone: out of bounds",
                [&](auto& f) { put32(f.data() + phone.at + 56, static_cast<uint32_t>(f.size())); });
  expectRefusal(file, "an unaligned offset", "tensor am.phone: out of bounds",
                [&](auto& f) { put32(f.data() + phone.at + 56, phone.offset + 4); });
  expectRefusal(file, "a size past the end", "tensor am.phone: out of bounds",
                [&](auto& f) { put32(f.data() + phone.at + 60, 0xFFFFFFF0u); });
  expectRefusal(file, "a kind", "tensor am.phone: unknown kind", [&](auto& f) { put32(f.data() + phone.at + 40, 7); });
  expectRefusal(file, "a size", "tensor am.phone: wrong size",
                [&](auto& f) { put32(f.data() + phone.at + 44, phone.rows + 1); });
  expectRefusal(file, "a shape whose size overflows", "tensor am.phone: wrong size", [&](auto& f) {
    put32(f.data() + phone.at + 44, 0xFFFFFFFFu);
    put32(f.data() + phone.at + 48, 0x40000001u);
    put32(f.data() + phone.at + 52, 0x40000001u);
  });
  expectRefusal(file, "a stride", "tensor am.enc.0.pw1: wrong size",
                [&](auto& f) { put32(f.data() + pw1.at + 52, pw1.stride + 16); });
  expectRefusal(file, "a weight of -128", "tensor am.enc.0.pw1: weight -128",
                [&](auto& f) { f[pw1.offset + 5] = 0x80; });
  expectRefusal(file, "padding", "tensor am.enc.0.pw1: padding", [&](auto& f) {
    put32(f.data() + pw1.at + 48, pw1.cols - 1);  // the last column becomes padding
  });
  expectRefusal(file, "a NaN", "tensor am.phone: not finite",
                [&](auto& f) { put32(f.data() + phone.offset + 8, 0x7FC00000u); });
  expectRefusal(file, "a name", "bad tensor name", [&](auto& f) { std::memset(f.data() + phone.at, 'a', 40); });
  expectRefusal(file, "a missing tensor", "missing tensor am.phone", [&](auto& f) { f[phone.at + 7] = 'X'; });
  expectRefusal(file, "a duplicate", "duplicate tensor am.phone",
                [&](auto& f) { std::memcpy(f.data() + pw1.at, "am.phone\0", 9); });
  expectRefusal(file, "a config value", "config: d_enc out of range", [&](auto& f) { put32(f.data() + config.offset, 0); });
  expectRefusal(file, "an even kernel", "config: enc_kernel out of range",
                [&](auto& f) { put32(f.data() + config.offset + 4, 4); });
  expectRefusal(file, "a dilation", "config: enc_dilations out of range",
                [&](auto& f) { put32(f.data() + config.offset + 15 * 4, 0); });
  expectRefusal(file, "a config that disagrees", "tensor am.dur.c1: wrong shape",
                [&](auto& f) { put32(f.data() + config.offset + 4 * 4, 17); });
  expectRefusal(file, "a config length", "config: wrong length",
                [&](auto& f) { put32(f.data() + config.offset + 2 * 4, 2); });
}

// ---- tokens and chunks ----

void addWord(Plan& plan, std::size_t phones) {
  for (std::size_t i = 0; i < phones; ++i) {
    plan.phones[plan.count] = {i % 2 ? PhoneId::Ae : PhoneId::T, static_cast<uint8_t>(i % 2 ? Primary : 0)};
    plan.boundaries[plan.count++] = 0;
  }
  plan.phones[plan.count - 1].prosody |= WordEnd;
}

void addPause(Plan& plan, char mark) {
  plan.phones[plan.count] = {PhoneId::Silence, 0};
  plan.boundaries[plan.count++] = mark;
}

// A phrase of `words` words of `phones` phones each, then a pause.
void addPhrase(Plan& plan, std::size_t words, std::size_t phones, char mark) {
  for (std::size_t w = 0; w < words; ++w) addWord(plan, phones);
  addPause(plan, mark);
}

std::vector<Chunk> chunks(const Plan& plan) {
  std::vector<Chunk> out;
  splitPlan(plan, out);
  std::size_t at = 0;
  bool ok = true;
  for (const Chunk& chunk : out) {
    ok = ok && chunk.begin == at && chunk.end > chunk.begin && chunk.end - chunk.begin <= kMaxChunkPhones;
    at = chunk.end;
  }
  check(ok && at == plan.count, "chunks cover the plan in order, none too long");
  return out;
}

void testChunks(const std::vector<Utterance>& utterances) {
  for (const Utterance& utterance : utterances) {
    const auto plan = planOf(utterance);
    const std::vector<Chunk> split = chunks(*plan);
    check(split.size() == utterance.chunks.size(), "the fixture's chunks");
    for (std::size_t c = 0; c < std::min(split.size(), utterance.chunks.size()); ++c) {
      std::vector<Token> tokens(kMaxChunkPhones + 1);
      const std::size_t n = chunkTokens(*plan, split[c], tokens.data());
      bool same = n == utterance.chunks[c].codes.size();
      for (std::size_t i = 0; same && i < n; ++i) same = code(tokens[i]) == utterance.chunks[c].codes[i];
      check(same, "contract v1 codes, BOS first");
    }
  }

  Plan plan;
  addPhrase(plan, 3, 4, '.');
  addPhrase(plan, 2, 5, '!');
  auto split = chunks(plan);
  check(split.size() == 2 && split[0].end == 13, "a chunk per sentence");

  plan = {};
  addPhrase(plan, 1, 3, '.');
  addPhrase(plan, 3, 4, '?');
  addPhrase(plan, 1, 2, '.');
  split = chunks(plan);
  check(split.size() == 1, "short sentences join their neighbours");

  plan = {};
  for (int i = 0; i < 6; ++i) addPhrase(plan, 5, 6, i == 5 ? '.' : ',');  // 6 phrases of 31 phones
  addPhrase(plan, 3, 4, '!');
  split = chunks(plan);
  check(split.size() == 3 && split[0].end == 124 && split[1].end == 186 && split[2].end == plan.count,
        "a long sentence breaks after its last pause that fits");

  plan = {};
  addPhrase(plan, 60, 5, '.');  // 301 phones without a pause inside
  split = chunks(plan);
  bool words = split.size() == 3;
  for (std::size_t i = 0; words && i + 1 < split.size(); ++i) words = plan.phones[split[i].end - 1].prosody & WordEnd;
  check(words && split[0].end == 125, "a long phrase breaks after a whole word");

  plan = {};
  addPhrase(plan, 2, 3, ',');
  plan.phones[0].prosody = 3;
  check(!tokenizable(plan), "a stress outside the contract");
  plan.phones[0] = {static_cast<PhoneId>(SpeechModel::kPhones), 0};
  check(!tokenizable(plan), "a phone outside the contract");
}

// ---- parity with the reference runtime ----

void testKernels() {
  std::mt19937 random(7);
  std::uniform_int_distribution<int> value(-127, 127);
  for (std::size_t n : {16u, 32u, 304u, 1024u}) {
    std::vector<int8_t> a(n), b(n);
    int32_t expected = 0;
    for (std::size_t i = 0; i < n; ++i) {
      a[i] = static_cast<int8_t>(i % 5 ? value(random) : (i % 2 ? 127 : -127));
      b[i] = static_cast<int8_t>(i % 7 ? value(random) : -127);
      expected += a[i] * b[i];
    }
    check(kernels::dot(a.data(), b.data(), n) == expected, "the int8 dot product of " + std::to_string(n));
  }
  const float x[5] = {0.0f, 1.0f, -0.5f * 127.0f / 127.0f, 63.5f, -127.0f};
  int8_t codes[16];
  const float scale = kernels::quantize(x, 5, codes, 16);
  check(scale == 1.0f && codes[1] == 1 && codes[2] == -1 && codes[3] == 64 && codes[4] == -127 && codes[15] == 0,
        "quantisation rounds half away from zero and pads");
  const float zeros[3] = {};
  check(kernels::quantize(zeros, 3, codes, 16) == 1.0f && codes[0] == 0, "an all-zero vector has scale 1");
  const float nan[2] = {std::nanf(""), 1.0f};
  kernels::quantize(nan, 2, codes, 16);
  check(codes[0] == 127, "NaN quantises to the limit");
}

void testAcoustic(const SpeechModel& model, const std::vector<Utterance>& utterances) {
  SpeechScratch scratch(model);
  AcousticStream acoustic(model, scratch);
  std::vector<float> mel(acoustic.maxFrames() * model.mels);
  double worstMel = 0, worstPitch = 0;
  bool durations = true, frames = true;
  for (const Utterance& utterance : utterances) {
    for (const ChunkReference& chunk : utterance.chunks) {
      std::vector<Token> tokens;
      for (uint16_t c : chunk.codes) tokens.push_back(token(c));
      acoustic.begin(tokens.data(), tokens.size());
      for (std::size_t i = 0; i < tokens.size(); ++i) {
        durations = durations && acoustic.durations()[i] == chunk.durations[i];
        worstPitch = std::max(worstPitch, static_cast<double>(std::fabs(acoustic.pitch()[i] - chunk.pitch[i])));
      }
      frames = frames && acoustic.frames() == chunk.frames;
      std::vector<float> all;
      while (!acoustic.done()) {
        const std::size_t n = acoustic.next(mel.data());
        all.insert(all.end(), mel.begin(), mel.begin() + static_cast<std::ptrdiff_t>(n * model.mels));
      }
      frames = frames && all.size() == chunk.mel.size();
      float peak = 0;
      for (float v : chunk.mel) peak = std::max(peak, std::fabs(v));
      for (std::size_t i = 0; i < std::min(all.size(), chunk.mel.size()); ++i)
        worstMel = std::max(worstMel, static_cast<double>(std::fabs(all[i] - chunk.mel[i]) / peak));
    }
  }
  std::printf("acoustic: durations %s, largest pitch difference %.2e, largest mel difference %.2e of the peak\n",
              durations ? "equal" : "DIFFER", worstPitch, worstMel);
  check(durations && frames, "durations and frame counts equal the reference");
  check(worstPitch < 1e-4, "pitch within 1e-4 of the reference");
  check(worstMel < 1e-3, "mel frames within 1e-3 of the reference's peak");
}

void testVocoder(const SpeechModel& model, const std::vector<Utterance>& utterances) {
  SpeechScratch scratch(model);
  const std::size_t maxInput = 7;
  double worst = 1;
  int largest = 0;
  bool lengths = true;
  for (const Utterance& utterance : utterances) {
    // One stream per utterance: the DC blocker runs on from chunk to chunk.
    VocoderStream vocoder(model, scratch, maxInput);
    std::vector<int16_t> samples(vocoder.maxSamples()), pcm;
    for (const ChunkReference& chunk : utterance.chunks) {
      vocoder.begin(chunk.frames);
      // Uneven steps: the windows must give the same result whatever arrives when.
      for (std::size_t at = 0, step = 1; at < chunk.frames; at += step, step = step % maxInput + 1) {
        const std::size_t n = std::min(step, chunk.frames - at);
        const std::size_t written = vocoder.push(chunk.mel.data() + at * model.mels, n, samples.data());
        pcm.insert(pcm.end(), samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(written));
      }
      lengths = lengths && vocoder.done();
    }
    lengths = lengths && pcm.size() == utterance.pcm.size();
    worst = std::min(worst, correlation(pcm, utterance.pcm));
    largest = std::max(largest, maxDifference(pcm, utterance.pcm));
  }
  std::printf("vocoder: lowest waveform correlation %.7f, largest sample difference %d\n", worst, largest);
  check(lengths, "the vocoder gives (frames - 1) * hop samples per chunk");
  check(worst >= 0.99995 && largest <= 8, "the vocoder's samples match the reference");
}

void testVoice(const std::shared_ptr<const SpeechModel>& model, const std::vector<Utterance>& utterances) {
  ModelVoice voice(model);
  check(voice.rate() == 24000, "the voice's rate");
  double worst = 1;
  int largest = 0;
  bool lengths = true;
  for (const Utterance& utterance : utterances) {
    const auto plan = planOf(utterance);
    auto spoken = voice.start(*plan);
    check(spoken != nullptr, "the voice starts");
    if (!spoken) continue;
    const std::vector<int16_t> pcm = renderAll(*spoken, 333);
    lengths = lengths && pcm.size() == utterance.pcm.size();
    worst = std::min(worst, correlation(pcm, utterance.pcm));
    largest = std::max(largest, maxDifference(pcm, utterance.pcm));
    check(spoken->render(nullptr, 512) == 0, "an utterance that is over stays over");
  }
  std::printf("voice: lowest waveform correlation %.7f, largest sample difference %d\n", worst, largest);
  check(lengths, "the voice renders every sample of the reference");
  check(worst >= 0.9995, "plan to samples correlates with the reference at 0.9995 or better");

  Plan empty;
  check(!voice.start(empty), "an empty plan is refused");
  Plan wrong = *planOf(utterances[0]);
  wrong.phones[0].prosody = 3;
  check(!voice.start(wrong), "a plan outside the token contract is refused");
}

// A plan of the fixture's utterances repeated, several chunks long.
std::unique_ptr<Plan> longPlan(const std::vector<Utterance>& utterances, std::size_t phones) {
  auto plan = std::make_unique<Plan>();
  while (plan->count < phones) {
    for (const Utterance& utterance : utterances) {
      const auto part = planOf(utterance);
      for (std::size_t i = 0; i < part->count && plan->count < kMaxPhones; ++i) {
        plan->phones[plan->count] = part->phones[i];
        plan->boundaries[plan->count++] = part->boundaries[i];
      }
    }
  }
  return plan;
}

void testCancel(const std::shared_ptr<const SpeechModel>& model, const std::vector<Utterance>& utterances) {
  ModelVoice voice(model);
  const auto plan = longPlan(utterances, 400);
  auto spoken = voice.start(*plan);
  int16_t block[512];
  bool rendering = true;
  for (int i = 0; i < 3; ++i) rendering = rendering && spoken->render(block, 512) > 0;
  check(rendering, "a long utterance renders");
  spoken->cancel();
  check(spoken->render(block, 512) == 0 && spoken->render(block, 512) == 0, "after cancel() render() gives 0");

  // From another thread, while samples are still waiting in the utterance.
  spoken = voice.start(*plan);
  std::atomic<bool> first{false}, cancelled{false};
  std::thread canceller([&] {
    while (!first) std::this_thread::yield();
    spoken->cancel();
    cancelled = true;
  });
  check(spoken->render(block, 100) == 100, "the first samples");
  first = true;
  while (!cancelled) std::this_thread::yield();
  check(spoken->render(block, 100) == 0, "a cancel from another thread ends the utterance");
  canceller.join();
}

// ---- a voice of the shipped size ----

class Writer {
 public:
  explicit Writer(uint32_t seed) : random_(seed) {}

  void u32(const std::string& name, const std::vector<uint32_t>& values) {
    std::vector<uint8_t> blob(values.size() * 4);
    for (std::size_t i = 0; i < values.size(); ++i) put32(blob.data() + 4 * i, values[i]);
    add(name, 2, 1, static_cast<uint32_t>(values.size()), static_cast<uint32_t>(values.size()), std::move(blob));
  }
  void f32(const std::string& name, std::size_t rows, std::size_t cols, float mean, float spread) {
    std::normal_distribution<float> normal(mean, spread);
    std::vector<uint8_t> blob(rows * cols * 4);
    for (std::size_t i = 0; i < rows * cols; ++i) {
      const float v = normal(random_);
      std::memcpy(blob.data() + 4 * i, &v, 4);
    }
    add(name, 0, static_cast<uint32_t>(rows), static_cast<uint32_t>(cols), static_cast<uint32_t>(cols), std::move(blob));
  }
  void dense(const std::string& name, std::size_t rows, std::size_t cols, float bias = 0.0f, float gain = 1.0f) {
    const std::size_t stride = (cols + 15) / 16 * 16;
    std::uniform_int_distribution<int> weight(-127, 127);
    std::vector<uint8_t> blob(rows * stride + rows * 8, 0);
    for (std::size_t r = 0; r < rows; ++r)
      for (std::size_t c = 0; c < cols; ++c) blob[r * stride + c] = static_cast<uint8_t>(static_cast<int8_t>(weight(random_)));
    const float scale = gain * 1.7f / (127.0f * std::sqrt(static_cast<float>(cols)));
    for (std::size_t r = 0; r < rows; ++r) {
      std::memcpy(blob.data() + rows * stride + 4 * r, &scale, 4);
      std::memcpy(blob.data() + rows * stride + 4 * rows + 4 * r, &bias, 4);
    }
    add(name, 1, static_cast<uint32_t>(rows), static_cast<uint32_t>(cols), static_cast<uint32_t>(stride), std::move(blob));
  }
  void norm(const std::string& name, std::size_t width) {
    f32(name + ".g", width, 1, 1.0f, 0.1f);
    f32(name + ".b", width, 1, 0.0f, 0.1f);
  }
  void block(const std::string& name, std::size_t width, std::size_t kernel) {
    f32(name + ".dw.w", width, kernel, 0.0f, 0.4f);
    f32(name + ".dw.b", width, 1, 0.0f, 0.1f);
    norm(name + ".ln", width);
    dense(name + ".pw1", width * 2, width);
    dense(name + ".pw2", width, width * 2);
    f32(name + ".gamma", width, 1, 0.5f, 0.1f);
  }
  void predictor(const std::string& name, std::size_t input, std::size_t width, float bias) {
    dense(name + ".c1", width, input * 3);
    norm(name + ".ln1", width);
    dense(name + ".c2", width, width * 3);
    norm(name + ".ln2", width);
    dense(name + ".out", 1, width, bias, 0.05f);
  }

  std::vector<uint8_t> bytes() const {
    const std::size_t header = (64 + 64 * tensors_.size() + 15) / 16 * 16;
    std::vector<uint8_t> file(header, 0);
    for (std::size_t i = 0; i < tensors_.size(); ++i) {
      const Tensor& t = tensors_[i];
      uint8_t* e = file.data() + 64 + 64 * i;
      std::memcpy(e, t.name.c_str(), t.name.size());
      const uint32_t fields[] = {t.kind, t.rows, t.cols, t.stride, static_cast<uint32_t>(file.size()),
                                 static_cast<uint32_t>(t.blob.size())};
      for (int k = 0; k < 6; ++k) put32(e + 40 + 4 * k, fields[k]);
      file.insert(file.end(), t.blob.begin(), t.blob.end());
      file.resize((file.size() + 15) / 16 * 16, 0);
    }
    std::memcpy(file.data(), "ATTS", 4);
    const uint32_t fields[] = {1, static_cast<uint32_t>(header), static_cast<uint32_t>(file.size()), 0,
                               static_cast<uint32_t>(tensors_.size()), 24000, 256, 1024, 100, 1, 0};
    for (int k = 0; k < 11; ++k) put32(file.data() + 4 + 4 * k, fields[k]);
    put32(file.data() + 16, crc32(file.data(), file.size()));
    return file;
  }

 private:
  struct Tensor {
    std::string name;
    uint32_t kind, rows, cols, stride;
    std::vector<uint8_t> blob;
  };
  void add(const std::string& name, uint32_t kind, uint32_t rows, uint32_t cols, uint32_t stride,
           std::vector<uint8_t> blob) {
    tensors_.push_back({name, kind, rows, cols, stride, std::move(blob)});
  }
  std::mt19937 random_;
  std::vector<Tensor> tensors_;
};

// The layout of the model's design: d_enc 96, d_pred 48, d_dec 128, a vocoder of 128 channels with
// five blocks; durations around seven frames, as speech has.
std::vector<uint8_t> shippedSizeVoice() {
  Writer w(1);
  w.u32("config", {96, 5, 3, 2, 48, 128, 7, 2, 2, 128, 3, 7, 5, 2, 255, 1, 2, 4});
  w.f32("am.phone", 41, 96, 0.0f, 0.5f);
  w.f32("am.stress", 3, 96, 0.0f, 0.5f);
  w.f32("am.wordend", 2, 96, 0.0f, 0.5f);
  w.f32("am.punct", 8, 96, 0.0f, 0.5f);
  for (int i = 0; i < 3; ++i) w.block("am.enc." + std::to_string(i), 96, 5);
  w.norm("am.enc_ln", 96);
  w.predictor("am.dur", 96, 48, 2.0f);
  w.predictor("am.pitch", 96, 48, 0.0f);
  w.dense("am.pitch_emb", 96, 3);
  w.dense("am.frame_in", 128, 98);
  for (int i = 0; i < 2; ++i) w.block("am.dec." + std::to_string(i), 128, 7);
  w.norm("am.dec_ln", 128);
  w.dense("am.mel_out", 100, 128);
  w.dense("voc.in", 128, 300);
  w.norm("voc.in_ln", 128);
  for (int i = 0; i < 5; ++i) w.block("voc.blk." + std::to_string(i), 128, 7);
  w.norm("voc.out_ln", 128);
  w.dense("voc.head", 1026, 128, 0.0f, 0.3f);
  return w.bytes();
}

void benchmark(const std::vector<Utterance>& utterances) {
  const std::vector<uint8_t> file = shippedSizeVoice();
  std::string error;
  const auto model = SpeechModel::parse(file.data(), file.size(), error);
  check(model != nullptr, "a voice of the shipped size loads: " + error);
  if (!model) return;
  ModelVoice voice(model);
  const auto plan = longPlan(utterances, 120);
  const auto started = Clock::now();
  auto spoken = voice.start(*plan);
  int16_t block[512];
  std::size_t samples = spoken->render(block, 512);
  const double first = std::chrono::duration<double>(Clock::now() - started).count();
  while (const std::size_t n = spoken->render(block, 512)) samples += n;
  const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
  const double audio = static_cast<double>(samples) / model->sampleRate;
  std::printf("benchmark: %zu bytes, %zu phones, %.2f s of audio in %.3f s: real-time factor %.4f, first samples "
              "after %.1f ms\n",
              file.size(), plan->count, audio, seconds, seconds / audio, first * 1000);
}

}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fputs("usage: tc002-speech-voice-test FIXTURE_DIRECTORY\n", stderr);
    return 2;
  }
  const std::string dir = argv[1];
  const std::vector<uint8_t> file = readFile(dir + "/voice.atts");
  std::string error;
  const auto model = SpeechModel::parse(file.data(), file.size(), error);
  if (!model) {
    std::printf("FAIL: the fixture voice: %s\n", error.c_str());
    return 1;
  }
  const std::vector<Utterance> utterances = readExpectations(dir + "/voice.expect", model->mels);
  testKernels();
  testLoader(dir, file);
  testChunks(utterances);
  testAcoustic(*model, utterances);
  testVocoder(*model, utterances);
  testVoice(model, utterances);
  testCancel(model, utterances);
  benchmark(utterances);
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::puts("speech voice: all passed");
  return 0;
}
