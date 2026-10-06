#include "platform/posix/Bytes.h"
#include "core/payload/Crc.h"
#include "platform/tc002/speech/SpeechModel.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <map>

#include "platform/posix/Files.h"

namespace awtrix::speech {
namespace {

constexpr std::size_t kHeaderBytes = 64;
constexpr std::size_t kEntryBytes = 64;
constexpr std::size_t kNameBytes = 40;
constexpr std::size_t kMaxTensors = 1024;
constexpr uint32_t kVersion = 1;
constexpr uint32_t kTokenContract = 1;
constexpr uint32_t kF32 = 0, kDense = 1, kU32 = 2;

// The hyper-parameters at the start of the config tensor, in this order; the encoder dilations
// follow them.
enum Key { DEnc, EncKernel, EncBlocks, EncRatio, DPred, DDec, DecKernel, DecBlocks, DecRatio, VocDim, VocInKernel,
           VocKernel, VocBlocks, VocRatio, MaxDuration, kKeys };
const char* const kKeyNames[kKeys] = {"d_enc", "enc_kernel", "enc_blocks", "enc_ratio", "d_pred", "d_dec",
                                      "dec_kernel", "dec_blocks", "dec_ratio", "voc_dim", "voc_in_kernel",
                                      "voc_kernel", "voc_blocks", "voc_ratio", "max_duration"};
struct Range {
  uint32_t low, high;
  bool odd;
};
const Range kRanges[kKeys] = {{1, 1024, false}, {1, 15, true}, {1, 16, false}, {1, 8, false}, {1, 1024, false},
                              {1, 1024, false}, {1, 15, true}, {1, 16, false}, {1, 8, false}, {1, 1024, false},
                              {1, 15, true},    {1, 15, true}, {1, 16, false}, {1, 8, false}, {1, 255, false}};

bool finite(const float* values, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i)
    if (!std::isfinite(values[i])) return false;
  return true;
}

struct Entry {
  uint32_t kind, rows, cols, stride, offset, bytes;
};

// Looks tensors up by name and checks each against the shape the config gives it.
class Tensors {
 public:
  Tensors(const uint8_t* file, std::string& error) : file_(file), error_(error) {}

  bool add(const std::string& name, const Entry& entry) {
    return tensors_.emplace(name, entry).second || fail("duplicate tensor " + name);
  }

  bool fail(const std::string& reason) {
    error_ = reason;
    return false;
  }

  const Entry* find(const std::string& name, uint32_t kind, std::size_t rows, std::size_t cols) {
    const auto it = tensors_.find(name);
    if (it == tensors_.end()) {
      fail("missing tensor " + name);
      return nullptr;
    }
    const Entry& entry = it->second;
    if (entry.kind != kind || entry.rows != rows || entry.cols != cols) {
      fail("tensor " + name + ": wrong shape");
      return nullptr;
    }
    return &entry;
  }

  const uint32_t* config(std::size_t& count) {
    const auto it = tensors_.find("config");
    if (it == tensors_.end() || it->second.kind != kU32 || it->second.rows != 1) {
      fail("missing tensor config");
      return nullptr;
    }
    count = it->second.cols;
    return reinterpret_cast<const uint32_t*>(file_ + it->second.offset);
  }

  bool matrix(const std::string& name, std::size_t rows, std::size_t cols, const float*& out) {
    const Entry* entry = find(name, kF32, rows, cols);
    if (!entry) return false;
    out = reinterpret_cast<const float*>(file_ + entry->offset);
    return true;
  }

  bool vector(const std::string& name, std::size_t count, const float*& out) {
    return matrix(name, count, 1, out);
  }

  bool norm(const std::string& name, std::size_t width, NormLayer& out) {
    return vector(name + ".g", width, out.gains) && vector(name + ".b", width, out.biases);
  }

  bool dense(const std::string& name, std::size_t rows, std::size_t cols, DenseLayer& out) {
    const Entry* entry = find(name, kDense, rows, cols);
    if (!entry) return false;
    out.weights = reinterpret_cast<const int8_t*>(file_ + entry->offset);
    out.scales = reinterpret_cast<const float*>(file_ + entry->offset + std::size_t{entry->rows} * entry->stride);
    out.biases = out.scales + rows;
    out.rows = rows;
    out.cols = cols;
    out.stride = entry->stride;
    return true;
  }

  bool block(const std::string& name, std::size_t width, std::size_t kernel, std::size_t ratio,
             std::size_t dilation, BlockLayer& out) {
    out.width = width;
    out.kernel = kernel;
    out.dilation = dilation;
    return matrix(name + ".dw.w", width, kernel, out.depthwise) &&
           vector(name + ".dw.b", width, out.depthwiseBiases) && norm(name + ".ln", width, out.norm) &&
           dense(name + ".pw1", width * ratio, width, out.expand) &&
           dense(name + ".pw2", width, width * ratio, out.project) && vector(name + ".gamma", width, out.gamma);
  }

  bool predictor(const std::string& name, std::size_t input, std::size_t width, PredictorLayers& out) {
    return dense(name + ".c1", width, input * 3, out.first) && norm(name + ".ln1", width, out.firstNorm) &&
           dense(name + ".c2", width, width * 3, out.second) && norm(name + ".ln2", width, out.secondNorm) &&
           dense(name + ".out", 1, width, out.out);
  }

 private:
  const uint8_t* file_;
  std::string& error_;
  std::map<std::string, Entry> tensors_;
};

// A directory entry's name, kind, bounds and contents; dense weights must be zero padded and
// never -128, and floats finite.
bool checkEntry(const uint8_t* file, std::size_t size, std::size_t header, const uint8_t* raw, std::string& name,
                Entry& entry, std::string& error) {
  const auto* end = static_cast<const uint8_t*>(std::memchr(raw, 0, kNameBytes));
  if (!end || end == raw) {
    error = "bad tensor name";
    return false;
  }
  name.assign(reinterpret_cast<const char*>(raw), static_cast<std::size_t>(end - raw));
  for (const char c : name)
    if (c < '!' || c > '~') {
      error = "bad tensor name";
      return false;
    }
  entry = {posix::le32(raw + 40), posix::le32(raw + 44), posix::le32(raw + 48), posix::le32(raw + 52), posix::le32(raw + 56), posix::le32(raw + 60)};
  const auto fail = [&](const char* reason) {
    error = "tensor " + name + ": " + reason;
    return false;
  };
  if (entry.offset % 16 || entry.offset < header || uint64_t{entry.offset} + entry.bytes > size)
    return fail("out of bounds");
  if (!entry.rows || !entry.cols) return fail("empty");
  // Each below the file size, no product of them overflows.
  if (entry.rows > size || entry.cols > size || entry.stride > size) return fail("wrong size");
  const uint64_t cells = uint64_t{entry.rows} * entry.cols;
  const uint8_t* data = file + entry.offset;
  switch (entry.kind) {
    case kF32:
    case kU32:
      if (entry.stride != entry.cols || entry.bytes != cells * 4) return fail("wrong size");
      if (entry.kind == kF32 && !finite(reinterpret_cast<const float*>(data), cells)) return fail("not finite");
      return true;
    case kDense: {
      if (entry.stride != (uint64_t{entry.cols} + 15) / 16 * 16 ||
          entry.bytes != uint64_t{entry.rows} * entry.stride + uint64_t{entry.rows} * 8)
        return fail("wrong size");
      const auto* weights = reinterpret_cast<const int8_t*>(data);
      for (std::size_t r = 0; r < entry.rows; ++r) {
        const int8_t* row = weights + r * entry.stride;
        for (std::size_t c = 0; c < entry.stride; ++c) {
          if (c >= entry.cols ? row[c] != 0 : row[c] == -128) return fail(c >= entry.cols ? "padding" : "weight -128");
        }
      }
      if (!finite(reinterpret_cast<const float*>(data + std::size_t{entry.rows} * entry.stride),
                  std::size_t{entry.rows} * 2))
        return fail("not finite");
      return true;
    }
    default:
      return fail("unknown kind");
  }
}

}

uint32_t crc32(const uint8_t* data, std::size_t size) {
  return ~crc32Update(0xffffffffu, data, size);
}

std::shared_ptr<const SpeechModel> SpeechModel::load(const std::string& path, std::string& error) {
  posix::UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY));
  struct stat info{};
  if (!fd.valid() || ::fstat(fd.get(), &info) != 0) {
    error = std::strerror(errno);
    return nullptr;
  }
  if (!S_ISREG(info.st_mode)) {
    error = "not a file";
    return nullptr;
  }
  if (info.st_size < static_cast<off_t>(kHeaderBytes) || info.st_size > static_cast<off_t>(kMaxFileBytes)) {
    error = info.st_size < static_cast<off_t>(kHeaderBytes) ? "not an ATTS file" : "too large";
    return nullptr;
  }
  const auto size = static_cast<std::size_t>(info.st_size);
  std::shared_ptr<SpeechModel> model(new SpeechModel);
  model->storage_.resize((size + 15) / 16);
  if (!posix::readAll(fd.get(), model->storage_.data(), size)) {
    error = std::strerror(errno);
    return nullptr;
  }
  if (!model->resolve(size, error)) return nullptr;
  return model;
}

std::shared_ptr<const SpeechModel> SpeechModel::parse(const uint8_t* data, std::size_t size, std::string& error) {
  if (size > kMaxFileBytes) {
    error = "too large";
    return nullptr;
  }
  std::shared_ptr<SpeechModel> model(new SpeechModel);
  model->storage_.resize((size + 15) / 16);
  if (size) std::memcpy(model->storage_.data(), data, size);
  if (!model->resolve(size, error)) return nullptr;
  return model;
}

bool SpeechModel::resolve(std::size_t size, std::string& error) {
  const auto* file = reinterpret_cast<const uint8_t*>(storage_.data());
  const auto fail = [&](std::string reason) {
    error = std::move(reason);
    return false;
  };
  if (size < kHeaderBytes || std::memcmp(file, "ATTS", 4) != 0) return fail("not an ATTS file");
  if (posix::le32(file + 4) != kVersion) return fail("version " + std::to_string(posix::le32(file + 4)) + " not supported");
  if (posix::le32(file + 12) != size) return fail("size mismatch");
  std::array<uint8_t, 4> stored;
  std::memcpy(stored.data(), file + 16, 4);
  uint8_t* field = reinterpret_cast<uint8_t*>(storage_.data()) + 16;
  std::memset(field, 0, 4);
  const uint32_t crc = crc32(file, size);
  std::memcpy(field, stored.data(), 4);
  if (crc != posix::le32(stored.data())) return fail("checksum mismatch");

  const uint32_t count = posix::le32(file + 20);
  const uint64_t header = (kHeaderBytes + uint64_t{kEntryBytes} * count + 15) / 16 * 16;
  if (!count || count > kMaxTensors || posix::le32(file + 8) != header || header > size) return fail("bad header");
  sampleRate = posix::le32(file + 24);
  hop = posix::le32(file + 28);
  fftSize = posix::le32(file + 32);
  mels = posix::le32(file + 36);
  if (sampleRate != kSampleRate) return fail("sample rate " + std::to_string(sampleRate) + " not supported");
  if (posix::le32(file + 40) != kTokenContract)
    return fail("token contract " + std::to_string(posix::le32(file + 40)) + " not supported");
  if (posix::le32(file + 44) != 0) return fail("flags not supported");
  if (fftSize < 16 || fftSize > 4096 || (fftSize & (fftSize - 1)) || !hop || hop > fftSize)
    return fail("bad STFT");
  if (!mels || mels > 1024) return fail("bad mel count");

  Tensors tensors(file, error);
  for (uint32_t i = 0; i < count; ++i) {
    std::string name;
    Entry entry{};
    if (!checkEntry(file, size, static_cast<std::size_t>(header), file + kHeaderBytes + kEntryBytes * i, name,
                    entry, error) ||
        !tensors.add(name, entry))
      return false;
  }

  std::size_t values = 0;
  const uint32_t* config = tensors.config(values);
  if (!config) return false;
  if (values < kKeys) return fail("config: too short");
  for (int k = 0; k < kKeys; ++k) {
    const Range& range = kRanges[k];
    if (config[k] < range.low || config[k] > range.high || (range.odd && !(config[k] & 1)))
      return fail(std::string("config: ") + kKeyNames[k] + " out of range");
  }
  if (values != kKeys + std::size_t{config[EncBlocks]}) return fail("config: wrong length");
  for (std::size_t i = 0; i < config[EncBlocks]; ++i)
    if (config[kKeys + i] < 1 || config[kKeys + i] > 64) return fail("config: enc_dilations out of range");

  const std::size_t d = config[DEnc], p = config[DPred], e = config[DDec], v = config[VocDim];
  maxDuration = config[MaxDuration];
  vocoderInKernel = config[VocInKernel];
  if (!tensors.matrix("am.phone", kPhones, d, phones) || !tensors.matrix("am.stress", kStresses, d, stresses) ||
      !tensors.matrix("am.wordend", kWordEnds, d, wordEnds) ||
      !tensors.matrix("am.punct", kPunctuations, d, punctuations))
    return false;
  encoder.resize(config[EncBlocks]);
  for (std::size_t i = 0; i < encoder.size(); ++i)
    if (!tensors.block("am.enc." + std::to_string(i), d, config[EncKernel], config[EncRatio], config[kKeys + i],
                       encoder[i]))
      return false;
  decoder.resize(config[DecBlocks]);
  for (std::size_t i = 0; i < decoder.size(); ++i)
    if (!tensors.block("am.dec." + std::to_string(i), e, config[DecKernel], config[DecRatio], 1, decoder[i]))
      return false;
  vocoder.resize(config[VocBlocks]);
  for (std::size_t i = 0; i < vocoder.size(); ++i)
    if (!tensors.block("voc.blk." + std::to_string(i), v, config[VocKernel], config[VocRatio], 1, vocoder[i]))
      return false;
  return tensors.norm("am.enc_ln", d, encoderNorm) && tensors.predictor("am.dur", d, p, duration) &&
         tensors.predictor("am.pitch", d, p, pitch) && tensors.dense("am.pitch_emb", d, 3, pitchEmbedding) &&
         tensors.dense("am.frame_in", e, d + 2, frameIn) && tensors.norm("am.dec_ln", e, decoderNorm) &&
         tensors.dense("am.mel_out", mels, e, melOut) &&
         tensors.dense("voc.in", v, mels * vocoderInKernel, vocoderIn) && tensors.norm("voc.in_ln", v, vocoderInNorm) &&
         tensors.norm("voc.out_ln", v, vocoderNorm) && tensors.dense("voc.head", fftSize + 2, v, head);
}

}
