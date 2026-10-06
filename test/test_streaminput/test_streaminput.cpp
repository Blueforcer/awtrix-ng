#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/audio/Mp3Decoder.h"
#include "core/audio/StreamDecode.h"
#include "core/audio/StreamInputBuffer.h"
#include "core/radio/IcyStream.h"
#include "../test_mp3pcm/vectors.h"

using namespace awtrix;
using audio::StreamInputBuffer;

void setUp() {}
void tearDown() {}

namespace {

std::vector<uint8_t> readable(StreamInputBuffer& input) {
  const auto view = input.decoderView();
  if (!view.size) return {};
  return {view.data, view.data + view.size};
}

void shiftHead(StreamInputBuffer& input, std::size_t position) {
  std::vector<uint8_t> padding(position, 0);
  TEST_ASSERT_TRUE(input.push(padding.data(), padding.size()));
  TEST_ASSERT_TRUE(input.consume(position));
}

void test_wrap_overflow_and_consume_bounds_keep_every_byte() {
  StreamInputBuffer input(8);
  shiftHead(input, 6);
  const uint8_t first[] = {0, 1, 2, 3, 4, 5, 6};
  TEST_ASSERT_TRUE(input.push(first, sizeof(first)));
  TEST_ASSERT_EQUAL_size_t(7, input.size());
  TEST_ASSERT_EQUAL_size_t(1, input.room());
  TEST_ASSERT_TRUE(readable(input) == std::vector<uint8_t>(first, first + sizeof(first)));
  const uint8_t overflow[] = {7, 8};
  TEST_ASSERT_FALSE(input.push(overflow, sizeof(overflow)));
  TEST_ASSERT_FALSE(input.consume(9));
  TEST_ASSERT_TRUE(readable(input) == std::vector<uint8_t>(first, first + sizeof(first)));

  TEST_ASSERT_TRUE(input.consume(3));
  TEST_ASSERT_TRUE(input.push(overflow, sizeof(overflow)));
  const std::vector<uint8_t> expected{3, 4, 5, 6, 7, 8};
  TEST_ASSERT_TRUE(readable(input) == expected);
  TEST_ASSERT_TRUE(input.consume(input.size()));
  TEST_ASSERT_EQUAL_size_t(0, input.size());
  TEST_ASSERT_EQUAL_size_t(8, input.room());
}

void test_clear_null_input_and_zero_capacity() {
  StreamInputBuffer input(64 * 1024);
  const uint8_t bytes[] = {0xff, 0xfb, 0x90};
  TEST_ASSERT_TRUE(input.push(bytes, sizeof(bytes)));
  const auto first = input.decoderView();
  TEST_ASSERT_EQUAL_size_t(3, first.size);
  const uint8_t* base = first.data;
  TEST_ASSERT_TRUE(input.consume(2));
  TEST_ASSERT_FALSE(input.push(nullptr, 1));
  TEST_ASSERT_TRUE(input.push(nullptr, 0));
  input.clear();
  TEST_ASSERT_EQUAL_size_t(0, input.size());
  TEST_ASSERT_NULL(input.decoderView().data);
  TEST_ASSERT_TRUE(input.push(bytes, sizeof(bytes)));
  TEST_ASSERT_EQUAL_PTR(base, input.decoderView().data);

  StreamInputBuffer empty(0);
  TEST_ASSERT_FALSE(empty.push(bytes, 1));
  TEST_ASSERT_TRUE(empty.push(nullptr, 0));
  TEST_ASSERT_TRUE(empty.consume(0));
}

void test_view_bridges_wrap_only_after_short_tail() {
  StreamInputBuffer input(4096);
  shiftHead(input, 2096);
  std::vector<uint8_t> bytes(3000);
  for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i);
  TEST_ASSERT_TRUE(input.push(bytes.data(), bytes.size()));
  const auto direct = input.decoderView();
  TEST_ASSERT_EQUAL_size_t(2000, direct.size);
  TEST_ASSERT_EQUAL_MEMORY(bytes.data(), direct.data, direct.size);
  // NoSync preserves these three tail bytes so a header may span the physical wrap.
  TEST_ASSERT_TRUE(input.consume(direct.size - 3));
  const auto wrapped = input.decoderView();
  TEST_ASSERT_EQUAL_size_t(1003, wrapped.size);
  TEST_ASSERT_EQUAL_MEMORY(bytes.data() + 1997, wrapped.data, wrapped.size);
}

void test_mixed_traffic_matches_fifo_through_repeated_wraps() {
  constexpr std::size_t kCapacity = 4093;
  StreamInputBuffer input(kCapacity);
  std::vector<uint8_t> queued;
  uint32_t random = 0x12345678;
  for (unsigned step = 0; step < 5000; ++step) {
    random = random * 1664525u + 1013904223u;
    const std::size_t bytes = (random >> 8) % 3079;
    if (random & 1) {
      std::vector<uint8_t> chunk(bytes);
      for (std::size_t i = 0; i < bytes; ++i) chunk[i] = static_cast<uint8_t>(step + i);
      const bool fits = bytes <= input.room();
      TEST_ASSERT_EQUAL(fits, input.push(chunk.data(), chunk.size()));
      if (fits) queued.insert(queued.end(), chunk.begin(), chunk.end());
    } else {
      const bool fits = bytes <= queued.size();
      TEST_ASSERT_EQUAL(fits, input.consume(bytes));
      if (fits) queued.erase(queued.begin(), queued.begin() + bytes);
    }
    const auto view = input.decoderView();
    TEST_ASSERT_EQUAL_size_t(queued.size(), input.size());
    TEST_ASSERT_EQUAL_size_t(kCapacity, input.room() + input.size());
    TEST_ASSERT_TRUE(view.size <= queued.size());
    if (view.size) TEST_ASSERT_TRUE(std::equal(view.data, view.data + view.size, queued.begin()));
  }
}

void test_frame_bound_covers_all_supported_header_combinations() {
  std::size_t maximum = 0;
  for (unsigned version : {0u, 2u, 3u}) {
    for (unsigned bitrate = 1; bitrate <= 14; ++bitrate) {
      for (unsigned rate = 0; rate < 3; ++rate) {
        for (unsigned padding = 0; padding < 2; ++padding) {
          for (unsigned crc = 0; crc < 2; ++crc) {
            for (unsigned channels = 0; channels < 4; ++channels) {
              const uint8_t bytes[] = {
                  0xff, static_cast<uint8_t>(0xe0 | (version << 3) | 2 | crc),
                  static_cast<uint8_t>((bitrate << 4) | (rate << 2) | (padding << 1)),
                  static_cast<uint8_t>(channels << 6)};
              mp3::FrameHeader header;
              TEST_ASSERT_TRUE(mp3::parseHeader(bytes, sizeof(bytes), header));
              TEST_ASSERT_TRUE(mp3::isSupported(header));
              const std::size_t frame = static_cast<std::size_t>(header.frameBytes());
              TEST_ASSERT_TRUE(frame <= StreamInputBuffer::kMaxFrameBytes);
              maximum = std::max(maximum, frame);
            }
          }
        }
      }
    }
  }
  TEST_ASSERT_EQUAL_size_t(StreamInputBuffer::kMaxFrameBytes, maximum);
}

struct Decoded {
  std::vector<int16_t> pcm;
  // Successful-frame format sequence proves channels, rates and sample counts stay in order too.
  std::vector<int> format;
};

void appendFrame(Decoded& out, const mp3::DecodeResult& result, const int16_t* pcm) {
  if (result.status != mp3::DecodeStatus::Ok) return;
  out.pcm.insert(out.pcm.end(), pcm, pcm + result.samples * result.channels);
  out.format.insert(out.format.end(), {result.sampleRateHz, result.channels, result.samples});
}

Decoded decodeWhole(const uint8_t* data, std::size_t bytes) {
  mp3::Decoder decoder;
  std::vector<int16_t> pcm(mp3::kMaxPcmPerFrame);
  Decoded out;
  std::size_t position = 0;
  while (position < bytes) {
    const auto result = decoder.decode(data + position, bytes - position, pcm.data());
    if (!result.bytesConsumed) break;
    TEST_ASSERT_TRUE(result.bytesConsumed <= bytes - position);
    position += result.bytesConsumed;
    appendFrame(out, result, pcm.data());
  }
  return out;
}

Decoded decodeChunked(const uint8_t* data, std::size_t bytes, std::size_t capacity,
                      std::size_t chunkBytes, std::size_t initialHead,
                      int metaInt = 0, std::vector<std::string>* metadata = nullptr) {
  StreamInputBuffer input(capacity);
  shiftHead(input, initialHead);
  radio::MetadataSplitter splitter;
  splitter.reset(metaInt);
  mp3::Decoder decoder;
  std::vector<int16_t> pcm(mp3::kMaxPcmPerFrame);
  Decoded out;
  std::size_t position = 0;
  unsigned passes = 0;
  while (position < bytes || input.size()) {
    TEST_ASSERT_TRUE(++passes < 100000);
    const std::size_t take = std::min({chunkBytes, input.room(), bytes - position});
    if (take) {
      splitter.feed(data + position, take,
                    [&](const uint8_t* audio, std::size_t n) {
                      TEST_ASSERT_TRUE(input.push(audio, n));
                    },
                    [&](const std::string& block) {
                      if (metadata) metadata->push_back(block);
                    });
      position += take;
    }
    const bool progressed = audio::decodeFrames(
        input, 2,
        [&](const StreamInputBuffer::View& view) {
          const auto result = decoder.decode(view.data, view.size, pcm.data());
          TEST_ASSERT_TRUE(result.bytesConsumed <= view.size);
          return result;
        },
        [&](const mp3::DecodeResult& result) {
          appendFrame(out, result, pcm.data());
          return true;
        });
    if (!progressed && position == bytes) break;
    TEST_ASSERT_TRUE_MESSAGE(progressed || input.room(), "full ring stalled before a frame");
  }
  return out;
}

struct Vector {
  const char* name;
  const uint8_t* data;
  std::size_t bytes;
};

#define VECTOR(name) {#name, mp3vectors::k##name##_mp3, sizeof(mp3vectors::k##name##_mp3)}
const Vector kVectors[] = {
    VECTOR(sine_mono_64k), VECTOR(sine_stereo_128k), VECTOR(noise_stereo_320k),
    VECTOR(sweep_stereo_128k_48k), VECTOR(noise_stereo_32khz), VECTOR(clicks_stereo_128k),
    VECTOR(sine_mono_24khz_48k), VECTOR(noise_stereo_22khz), VECTOR(clicks_stereo_24khz),
    VECTOR(clicks_mono_16khz), VECTOR(noise_stereo_12khz), VECTOR(sine_mono_8khz)};
#undef VECTOR

void compare(const Vector& vector, std::size_t capacity, std::size_t chunkBytes,
             std::size_t initialHead, bool prefix = false) {
  // Deliberately invalid sync-like bytes exercise the three-byte carry without a valid header.
  std::vector<uint8_t> source;
  if (prefix) {
    for (unsigned i = 0; i < 120; ++i) source.insert(source.end(), {0xff, 0xe8, 0xff, 0x00});
  }
  source.insert(source.end(), vector.data, vector.data + vector.bytes);
  const auto whole = decodeWhole(source.data(), source.size());
  const auto streamed =
      decodeChunked(source.data(), source.size(), capacity, chunkBytes, initialHead);
  TEST_ASSERT_TRUE_MESSAGE(!whole.pcm.empty(), vector.name);
  TEST_ASSERT_TRUE_MESSAGE(whole.pcm == streamed.pcm, vector.name);
  TEST_ASSERT_TRUE_MESSAGE(whole.format == streamed.format, vector.name);
}

void test_wrapped_stream_pcm_matches_whole_input_for_every_version() {
  for (const auto& vector : kVectors) compare(vector, 4093, 137, 4090);
}

void test_one_byte_and_full_reads_with_corrupt_prefix_keep_pcm_and_reservoir() {
  compare(kVectors[2], 1451, 1, 1450, true);
  compare(kVectors[2], 4093, 1024, 2044, true);
  compare(kVectors[11], 4093, 31, 4091, true);
  compare(kVectors[2], 64 * 1024, 1024, 65533, true);
}

std::vector<uint8_t> withMetadata(const Vector& vector, std::size_t interval,
                                 const std::string& block, unsigned& count) {
  std::vector<uint8_t> stream;
  count = 0;
  for (std::size_t at = 0; at < vector.bytes;) {
    const std::size_t take = std::min(interval, vector.bytes - at);
    stream.insert(stream.end(), vector.data + at, vector.data + at + take);
    at += take;
    if (take == interval) {
      stream.push_back(static_cast<uint8_t>(block.size() / 16));
      stream.insert(stream.end(), block.begin(), block.end());
      ++count;
    }
  }
  return stream;
}

void test_icy_metadata_split_across_reads_does_not_enter_audio_ring() {
  const Vector& vector = kVectors[1];
  std::string block = "StreamTitle='Wrapped';";
  block.resize(32, '\0');
  unsigned count = 0;
  const auto stream = withMetadata(vector, 509, block, count);
  std::vector<std::string> metadata;
  const auto actual = decodeChunked(stream.data(), stream.size(), 4093, 113, 4090, 509, &metadata);
  const auto expected = decodeWhole(vector.data, vector.bytes);
  TEST_ASSERT_TRUE(actual.pcm == expected.pcm);
  TEST_ASSERT_TRUE(actual.format == expected.format);
  TEST_ASSERT_EQUAL_UINT(count, metadata.size());
  for (const auto& captured : metadata) TEST_ASSERT_TRUE(captured == block);
}

}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wrap_overflow_and_consume_bounds_keep_every_byte);
  RUN_TEST(test_clear_null_input_and_zero_capacity);
  RUN_TEST(test_view_bridges_wrap_only_after_short_tail);
  RUN_TEST(test_mixed_traffic_matches_fifo_through_repeated_wraps);
  RUN_TEST(test_frame_bound_covers_all_supported_header_combinations);
  RUN_TEST(test_wrapped_stream_pcm_matches_whole_input_for_every_version);
  RUN_TEST(test_one_byte_and_full_reads_with_corrupt_prefix_keep_pcm_and_reservoir);
  RUN_TEST(test_icy_metadata_split_across_reads_does_not_enter_audio_ring);
  return UNITY_END();
}
