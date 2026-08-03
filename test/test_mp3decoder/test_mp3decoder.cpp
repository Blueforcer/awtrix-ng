#include <unity.h>

#include <cstdint>
#include <vector>

#include "core/audio/Mp3Bitstream.h"
#include "core/audio/Mp3Frame.h"
#include "core/audio/Mp3Huffman.h"
#include "core/audio/Mp3SideInfo.h"

using namespace awtrix;

namespace {

std::vector<uint8_t> bytes(std::initializer_list<uint8_t> b) { return std::vector<uint8_t>(b); }

void test_reads_single_bits_msb_first() {
  const auto data = bytes({0b10110010});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(0, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(1, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(1, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(0, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(0, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(1, r.read(1));
  TEST_ASSERT_EQUAL_UINT32(0, r.read(1));
  TEST_ASSERT_TRUE(r.exhausted());
}

void test_reads_whole_byte() {
  const auto data = bytes({0xAB, 0xCD});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_EQUAL_UINT32(0xAB, r.read(8));
  TEST_ASSERT_EQUAL_UINT32(0xCD, r.read(8));
}

void test_read_straddles_byte_boundary() {
  const auto data = bytes({0xAB, 0xCD});
  mp3::BitReader r(data.data(), data.size());
  r.skip(4);
  TEST_ASSERT_EQUAL_UINT32(0xBC, r.read(8));
  TEST_ASSERT_EQUAL_UINT32(0xD, r.read(4));
}

void test_reads_up_to_24_bits_across_four_bytes() {
  const auto data = bytes({0xFF, 0x12, 0x34, 0x56, 0x78});
  mp3::BitReader r(data.data(), data.size());
  r.skip(4);
  TEST_ASSERT_EQUAL_UINT32(0xF12345, r.read(24));
  TEST_ASSERT_EQUAL_UINT32(0x67, r.read(8));
}

void test_read_zero_bits_is_a_no_op() {
  const auto data = bytes({0xFF});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.read(0));
  TEST_ASSERT_EQUAL_size_t(0, r.bitPos());
}

void test_peek_does_not_consume() {
  const auto data = bytes({0xAB, 0xCD});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_EQUAL_UINT32(0xA, r.peek(4));
  TEST_ASSERT_EQUAL_UINT32(0xA, r.peek(4));
  TEST_ASSERT_EQUAL_size_t(0, r.bitPos());
  TEST_ASSERT_EQUAL_UINT32(0xAB, r.read(8));
}

void test_tracks_position_and_remaining() {
  const auto data = bytes({0x00, 0x00, 0x00});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_EQUAL_size_t(24, r.bitsLeft());
  r.read(5);
  TEST_ASSERT_EQUAL_size_t(5, r.bitPos());
  TEST_ASSERT_EQUAL_size_t(19, r.bitsLeft());
  r.skip(19);
  TEST_ASSERT_EQUAL_size_t(0, r.bitsLeft());
  TEST_ASSERT_TRUE(r.exhausted());
}

void test_reading_past_the_end_yields_zero_and_latches_overrun() {
  const auto data = bytes({0xFF});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_FALSE(r.overrun());
  TEST_ASSERT_EQUAL_UINT32(0xF, r.read(4));
  TEST_ASSERT_EQUAL_UINT32(0, r.read(8));
  TEST_ASSERT_TRUE(r.overrun());
}

void test_overrun_is_sticky_across_later_valid_reads() {
  const auto data = bytes({0xFF, 0xFF});
  mp3::BitReader r(data.data(), data.size());
  r.skip(64);
  TEST_ASSERT_TRUE(r.overrun());
  TEST_ASSERT_EQUAL_UINT32(0, r.read(1));
  TEST_ASSERT_TRUE(r.overrun());
}

void test_skip_past_the_end_clamps_rather_than_wrapping() {
  const auto data = bytes({0xFF, 0xFF});
  mp3::BitReader r(data.data(), data.size());
  r.skip(1000);
  TEST_ASSERT_EQUAL_size_t(0, r.bitsLeft());
  TEST_ASSERT_TRUE(r.exhausted());
}

void test_peek_past_the_end_does_not_latch_overrun() {
  const auto data = bytes({0xFF});
  mp3::BitReader r(data.data(), data.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.peek(16));
  TEST_ASSERT_FALSE(r.overrun());
}

void test_empty_buffer_is_immediately_exhausted() {
  mp3::BitReader r(nullptr, 0);
  TEST_ASSERT_TRUE(r.exhausted());
  TEST_ASSERT_EQUAL_size_t(0, r.bitsLeft());
  TEST_ASSERT_EQUAL_UINT32(0, r.read(1));
  TEST_ASSERT_TRUE(r.overrun());
}

void test_seek_to_absolute_bit_position() {
  const auto data = bytes({0xAB, 0xCD});
  mp3::BitReader r(data.data(), data.size());
  r.read(16);
  r.seekBits(4);
  TEST_ASSERT_EQUAL_size_t(4, r.bitPos());
  TEST_ASSERT_EQUAL_UINT32(0xBC, r.read(8));
}

mp3::FrameHeader parsed(std::initializer_list<uint8_t> b) {
  const std::vector<uint8_t> v(b);
  mp3::FrameHeader h{};
  TEST_ASSERT_TRUE(mp3::parseHeader(v.data(), v.size(), h));
  return h;
}

bool rejects(std::initializer_list<uint8_t> b) {
  const std::vector<uint8_t> v(b);
  mp3::FrameHeader h{};
  return !mp3::parseHeader(v.data(), v.size(), h);
}

void test_parses_mpeg1_layer3_128k_joint_stereo() {
  const auto h = parsed({0xFF, 0xFB, 0x90, 0x64});
  TEST_ASSERT_EQUAL(mp3::Version::Mpeg1, h.version);
  TEST_ASSERT_EQUAL(mp3::Layer::LayerIII, h.layer);
  TEST_ASSERT_FALSE(h.hasCrc);
  TEST_ASSERT_EQUAL_INT(128, h.bitrateKbps);
  TEST_ASSERT_EQUAL_INT(44100, h.sampleRateHz);
  TEST_ASSERT_FALSE(h.padding);
  TEST_ASSERT_EQUAL(mp3::ChannelMode::JointStereo, h.channelMode);
  TEST_ASSERT_EQUAL_INT(2, h.modeExtension);
  TEST_ASSERT_EQUAL_INT(2, h.channels());
  TEST_ASSERT_EQUAL_INT(1152, h.samplesPerFrame());
}

void test_frame_length_is_floored_not_rounded() {
  TEST_ASSERT_EQUAL_INT(417, parsed({0xFF, 0xFB, 0x90, 0x64}).frameBytes());
}

void test_padding_adds_exactly_one_byte() {
  TEST_ASSERT_EQUAL_INT(418, parsed({0xFF, 0xFB, 0x92, 0x64}).frameBytes());
}

void test_parses_mono_64k() {
  const auto h = parsed({0xFF, 0xFB, 0x50, 0xC0});
  TEST_ASSERT_EQUAL(mp3::ChannelMode::Mono, h.channelMode);
  TEST_ASSERT_EQUAL_INT(1, h.channels());
  TEST_ASSERT_EQUAL_INT(64, h.bitrateKbps);
  TEST_ASSERT_EQUAL_INT(208, h.frameBytes());
}

void test_parses_320k_and_48khz() {
  TEST_ASSERT_EQUAL_INT(1044, parsed({0xFF, 0xFB, 0xE0, 0x64}).frameBytes());
  const auto h = parsed({0xFF, 0xFB, 0x94, 0x64});
  TEST_ASSERT_EQUAL_INT(48000, h.sampleRateHz);
  TEST_ASSERT_EQUAL_INT(384, h.frameBytes());
}

void test_crc_flag_is_inverted() {
  TEST_ASSERT_TRUE(parsed({0xFF, 0xFA, 0x90, 0x64}).hasCrc);
}

void test_parses_mpeg2_at_half_rate() {
  const auto h = parsed({0xFF, 0xF3, 0x50, 0xC0});
  TEST_ASSERT_EQUAL(mp3::Version::Mpeg2, h.version);
  TEST_ASSERT_EQUAL_INT(40, h.bitrateKbps);
  TEST_ASSERT_EQUAL_INT(22050, h.sampleRateHz);
  TEST_ASSERT_EQUAL_INT(576, h.samplesPerFrame());
  TEST_ASSERT_EQUAL_INT(130, h.frameBytes());
}

void test_free_format_reports_unknown_length() {
  const auto h = parsed({0xFF, 0xFB, 0x00, 0x64});
  TEST_ASSERT_EQUAL_INT(0, h.bitrateKbps);
  TEST_ASSERT_EQUAL_INT(0, h.frameBytes());
}

void test_rejects_reserved_and_invalid_encodings() {
  TEST_ASSERT_TRUE(rejects({0xFF, 0xF9, 0x90, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0xFD, 0x90, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0xFF, 0x90, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0xF1, 0x90, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0xFB, 0xF0, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0xFB, 0x9C, 0x64}));
}

void test_mpeg2_5_parses_but_is_not_supported() {
  const auto h = parsed({0xFF, 0xE3, 0x50, 0xC0});
  TEST_ASSERT_EQUAL(mp3::Version::Mpeg2_5, h.version);
  TEST_ASSERT_EQUAL_INT(11025, h.sampleRateHz);
  TEST_ASSERT_FALSE(mp3::isSupported(h));
}

void test_supported_covers_mpeg1_mono_and_stereo() {
  TEST_ASSERT_TRUE(mp3::isSupported(parsed({0xFF, 0xFB, 0x90, 0x64})));
  TEST_ASSERT_TRUE(mp3::isSupported(parsed({0xFF, 0xFB, 0x50, 0xC0})));
  TEST_ASSERT_TRUE(mp3::isSupported(parsed({0xFF, 0xFB, 0x90, 0x04})));
  TEST_ASSERT_TRUE(mp3::isSupported(parsed({0xFF, 0xFB, 0x90, 0x84})));
  TEST_ASSERT_FALSE(mp3::isSupported(parsed({0xFF, 0xF3, 0x50, 0xC0})));
}

void test_rejects_missing_sync_and_short_buffers() {
  TEST_ASSERT_TRUE(rejects({0xFE, 0xFB, 0x90, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0x7B, 0x90, 0x64}));
  TEST_ASSERT_TRUE(rejects({0xFF, 0xFB, 0x90}));
}

void test_find_sync_skips_leading_garbage() {
  const std::vector<uint8_t> v{0x49, 0x44, 0x33, 0xFF, 0x00, 0xFF, 0xFB, 0x90, 0x64};
  std::size_t at = 0;
  TEST_ASSERT_TRUE(mp3::findSync(v.data(), v.size(), 0, at));
  TEST_ASSERT_EQUAL_size_t(5, at);
}

void test_find_sync_reports_failure_on_a_buffer_without_one() {
  const std::vector<uint8_t> v{0x00, 0xFF, 0x00, 0xFF, 0xE0};
  std::size_t at = 0;
  TEST_ASSERT_FALSE(mp3::findSync(v.data(), v.size(), 0, at));
}

void test_table1_decodes_all_four_codewords() {
  const auto data = bytes({0xA0, 0x80});
  mp3::BitReader r(data.data(), data.size());
  mp3::Pair p{};

  TEST_ASSERT_TRUE(mp3::decodePair(r, mp3::kBigValueTables[1], p));
  TEST_ASSERT_EQUAL_INT(0, p.x);
  TEST_ASSERT_EQUAL_INT(0, p.y);
  TEST_ASSERT_EQUAL_size_t(1, r.bitPos());

  TEST_ASSERT_TRUE(mp3::decodePair(r, mp3::kBigValueTables[1], p));
  TEST_ASSERT_EQUAL_INT(1, p.x);
  TEST_ASSERT_EQUAL_INT(0, p.y);

  TEST_ASSERT_TRUE(mp3::decodePair(r, mp3::kBigValueTables[1], p));
  TEST_ASSERT_EQUAL_INT(1, p.x);
  TEST_ASSERT_EQUAL_INT(1, p.y);

  TEST_ASSERT_TRUE(mp3::decodePair(r, mp3::kBigValueTables[1], p));
  TEST_ASSERT_EQUAL_INT(0, p.x);
  TEST_ASSERT_EQUAL_INT(1, p.y);
  TEST_ASSERT_EQUAL_size_t(9, r.bitPos());
}

void test_table0_yields_zeros_without_consuming_bits() {
  const auto data = bytes({0xFF});
  mp3::BitReader r(data.data(), data.size());
  mp3::Pair p{1, 1};
  TEST_ASSERT_TRUE(mp3::decodePair(r, mp3::kBigValueTables[0], p));
  TEST_ASSERT_EQUAL_INT(0, p.x);
  TEST_ASSERT_EQUAL_INT(0, p.y);
  TEST_ASSERT_EQUAL_size_t(0, r.bitPos());
}

void test_count1_table_b_is_four_inverted_bits() {
  const auto data = bytes({0x0F});
  mp3::BitReader r(data.data(), data.size());
  mp3::Quad q{};

  TEST_ASSERT_TRUE(mp3::decodeQuad(r, mp3::kCount1Tables[1], q));
  TEST_ASSERT_EQUAL_INT(1, q.v);
  TEST_ASSERT_EQUAL_INT(1, q.w);
  TEST_ASSERT_EQUAL_INT(1, q.x);
  TEST_ASSERT_EQUAL_INT(1, q.y);

  TEST_ASSERT_TRUE(mp3::decodeQuad(r, mp3::kCount1Tables[0 + 1], q));
  TEST_ASSERT_EQUAL_INT(0, q.v);
  TEST_ASSERT_EQUAL_INT(0, q.w);
  TEST_ASSERT_EQUAL_INT(0, q.x);
  TEST_ASSERT_EQUAL_INT(0, q.y);
}

void test_count1_table_a_shortest_codeword_is_all_zero() {
  const auto data = bytes({0x80});
  mp3::BitReader r(data.data(), data.size());
  mp3::Quad q{1, 1, 1, 1};
  TEST_ASSERT_TRUE(mp3::decodeQuad(r, mp3::kCount1Tables[0], q));
  TEST_ASSERT_EQUAL_INT(0, q.v + q.w + q.x + q.y);
  TEST_ASSERT_EQUAL_size_t(1, r.bitPos());
}

void test_decode_fails_when_the_bits_run_out() {
  mp3::BitReader r(nullptr, 0);
  mp3::Pair p{};
  TEST_ASSERT_FALSE(mp3::decodePair(r, mp3::kBigValueTables[1], p));
}

void test_every_used_table_is_a_complete_prefix_code() {
  for (int t = 0; t < 32; ++t) {
    const mp3::HuffTable& table = mp3::kBigValueTables[t];
    if (table.codes == nullptr) continue;
    uint32_t weighted = 0;
    for (unsigned len = 1; len <= mp3::kMaxCodeLength; ++len)
      weighted += table.count[len] * (1u << (mp3::kMaxCodeLength - len));
    TEST_ASSERT_EQUAL_UINT32(1u << mp3::kMaxCodeLength, weighted);
  }
}

void test_codes_are_sorted_within_each_length() {
  for (int t = 0; t < 32; ++t) {
    const mp3::HuffTable& table = mp3::kBigValueTables[t];
    if (table.codes == nullptr) continue;
    for (unsigned len = 1; len <= mp3::kMaxCodeLength; ++len) {
      const unsigned first = table.first[len];
      for (unsigned i = 1; i < table.count[len]; ++i)
        TEST_ASSERT_TRUE(table.codes[first + i - 1] < table.codes[first + i]);
    }
  }
}

void test_unused_tables_are_absent() {
  TEST_ASSERT_NULL(mp3::kBigValueTables[0].codes);
  TEST_ASSERT_NULL(mp3::kBigValueTables[4].codes);
  TEST_ASSERT_NULL(mp3::kBigValueTables[14].codes);
}

void test_linbits_match_the_standard() {
  for (int t = 0; t <= 15; ++t) TEST_ASSERT_EQUAL_INT(0, mp3::kLinbits[t]);
  TEST_ASSERT_EQUAL_INT(1, mp3::kLinbits[16]);
  TEST_ASSERT_EQUAL_INT(13, mp3::kLinbits[23]);
  TEST_ASSERT_EQUAL_INT(4, mp3::kLinbits[24]);
  TEST_ASSERT_EQUAL_INT(13, mp3::kLinbits[31]);
  TEST_ASSERT_EQUAL_PTR(mp3::kBigValueTables[16].codes, mp3::kBigValueTables[23].codes);
  TEST_ASSERT_EQUAL_PTR(mp3::kBigValueTables[24].codes, mp3::kBigValueTables[31].codes);
}

class BitWriter {
 public:
  void put(uint32_t value, unsigned bits) {
    for (unsigned i = bits; i-- > 0;) {
      if (bitCount_ % 8 == 0) data_.push_back(0);
      if ((value >> i) & 1) data_.back() |= static_cast<uint8_t>(0x80 >> (bitCount_ % 8));
      ++bitCount_;
    }
  }
  const std::vector<uint8_t>& bytes() const { return data_; }

 private:
  std::vector<uint8_t> data_;
  std::size_t bitCount_ = 0;
};

void writeGranule(BitWriter& w, int part23, int bigValues, int gain, bool shortBlock) {
  w.put(part23, 12);
  w.put(bigValues, 9);
  w.put(gain, 8);
  w.put(0, 4);
  w.put(shortBlock, 1);
  if (shortBlock) {
    w.put(2, 2);
    w.put(0, 1);
    w.put(3, 5);
    w.put(5, 5);
    w.put(1, 3);
    w.put(2, 3);
    w.put(4, 3);
  } else {
    w.put(7, 5);
    w.put(11, 5);
    w.put(13, 5);
    w.put(9, 4);
    w.put(3, 3);
  }
  w.put(1, 1);
  w.put(0, 1);
  w.put(1, 1);
}

mp3::FrameHeader headerFor(int channels) {
  mp3::FrameHeader h{};
  h.version = mp3::Version::Mpeg1;
  h.layer = mp3::Layer::LayerIII;
  h.channelMode = channels == 1 ? mp3::ChannelMode::Mono : mp3::ChannelMode::Stereo;
  return h;
}

void test_mono_side_info_is_seventeen_bytes_and_round_trips() {
  BitWriter w;
  w.put(42, 9);
  w.put(0, 5);
  w.put(0b1010, 4);
  writeGranule(w, 1234, 200, 150, false);
  writeGranule(w, 999, 7, 210, true);

  const auto header = headerFor(1);
  TEST_ASSERT_EQUAL_INT(17, mp3::sideInfoBytes(header));
  TEST_ASSERT_EQUAL_size_t(17, w.bytes().size());

  mp3::BitReader r(w.bytes().data(), w.bytes().size());
  mp3::SideInfo si{};
  TEST_ASSERT_TRUE(mp3::parseSideInfo(r, header, si));
  TEST_ASSERT_EQUAL_size_t(17 * 8, r.bitPos());

  TEST_ASSERT_EQUAL_INT(42, si.mainDataBegin);
  TEST_ASSERT_TRUE(si.scfsi[0][0]);
  TEST_ASSERT_FALSE(si.scfsi[0][1]);
  TEST_ASSERT_TRUE(si.scfsi[0][2]);
  TEST_ASSERT_FALSE(si.scfsi[0][3]);

  const mp3::GranuleInfo& g0 = si.granules[0][0];
  TEST_ASSERT_EQUAL_INT(1234, g0.part2_3Length);
  TEST_ASSERT_EQUAL_INT(200, g0.bigValues);
  TEST_ASSERT_EQUAL_INT(150, g0.globalGain);
  TEST_ASSERT_FALSE(g0.windowSwitching);
  TEST_ASSERT_EQUAL_INT(0, g0.blockType);
  TEST_ASSERT_EQUAL_INT(7, g0.tableSelect[0]);
  TEST_ASSERT_EQUAL_INT(11, g0.tableSelect[1]);
  TEST_ASSERT_EQUAL_INT(13, g0.tableSelect[2]);
  TEST_ASSERT_EQUAL_INT(9, g0.region0Count);
  TEST_ASSERT_EQUAL_INT(3, g0.region1Count);
  TEST_ASSERT_TRUE(g0.preflag);
  TEST_ASSERT_TRUE(g0.count1TableSelect);
}

void test_switched_granule_infers_region_counts() {
  BitWriter w;
  w.put(0, 9);
  w.put(0, 5);
  w.put(0, 4);
  writeGranule(w, 100, 10, 100, true);
  writeGranule(w, 100, 10, 100, true);

  mp3::BitReader r(w.bytes().data(), w.bytes().size());
  mp3::SideInfo si{};
  TEST_ASSERT_TRUE(mp3::parseSideInfo(r, headerFor(1), si));

  const mp3::GranuleInfo& g = si.granules[0][0];
  TEST_ASSERT_TRUE(g.windowSwitching);
  TEST_ASSERT_EQUAL_INT(2, g.blockType);
  TEST_ASSERT_FALSE(g.mixedBlock);
  TEST_ASSERT_EQUAL_INT(8, g.region0Count);
  TEST_ASSERT_EQUAL_INT(12, g.region1Count);
  TEST_ASSERT_EQUAL_INT(3, g.tableSelect[0]);
  TEST_ASSERT_EQUAL_INT(5, g.tableSelect[1]);
  TEST_ASSERT_EQUAL_INT(1, g.subblockGain[0]);
  TEST_ASSERT_EQUAL_INT(2, g.subblockGain[1]);
  TEST_ASSERT_EQUAL_INT(4, g.subblockGain[2]);
}

void test_stereo_side_info_is_thirty_two_bytes() {
  BitWriter w;
  w.put(511, 9);
  w.put(0, 3);
  w.put(0, 4);
  w.put(0, 4);
  for (int gr = 0; gr < 2; ++gr)
    for (int ch = 0; ch < 2; ++ch) writeGranule(w, 500 + gr * 10 + ch, 50, 100, false);

  const auto header = headerFor(2);
  TEST_ASSERT_EQUAL_INT(32, mp3::sideInfoBytes(header));
  TEST_ASSERT_EQUAL_size_t(32, w.bytes().size());

  mp3::BitReader r(w.bytes().data(), w.bytes().size());
  mp3::SideInfo si{};
  TEST_ASSERT_TRUE(mp3::parseSideInfo(r, header, si));
  TEST_ASSERT_EQUAL_size_t(32 * 8, r.bitPos());
  TEST_ASSERT_EQUAL_INT(511, si.mainDataBegin);
  TEST_ASSERT_EQUAL_INT(500, si.granules[0][0].part2_3Length);
  TEST_ASSERT_EQUAL_INT(501, si.granules[0][1].part2_3Length);
  TEST_ASSERT_EQUAL_INT(510, si.granules[1][0].part2_3Length);
  TEST_ASSERT_EQUAL_INT(511, si.granules[1][1].part2_3Length);
}

void test_short_side_info_is_rejected() {
  const auto data = bytes({0x00, 0x00, 0x00});
  mp3::BitReader r(data.data(), data.size());
  mp3::SideInfo si{};
  TEST_ASSERT_FALSE(mp3::parseSideInfo(r, headerFor(1), si));
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reads_single_bits_msb_first);
  RUN_TEST(test_reads_whole_byte);
  RUN_TEST(test_read_straddles_byte_boundary);
  RUN_TEST(test_reads_up_to_24_bits_across_four_bytes);
  RUN_TEST(test_read_zero_bits_is_a_no_op);
  RUN_TEST(test_peek_does_not_consume);
  RUN_TEST(test_tracks_position_and_remaining);
  RUN_TEST(test_reading_past_the_end_yields_zero_and_latches_overrun);
  RUN_TEST(test_overrun_is_sticky_across_later_valid_reads);
  RUN_TEST(test_skip_past_the_end_clamps_rather_than_wrapping);
  RUN_TEST(test_peek_past_the_end_does_not_latch_overrun);
  RUN_TEST(test_empty_buffer_is_immediately_exhausted);
  RUN_TEST(test_seek_to_absolute_bit_position);

  RUN_TEST(test_parses_mpeg1_layer3_128k_joint_stereo);
  RUN_TEST(test_frame_length_is_floored_not_rounded);
  RUN_TEST(test_padding_adds_exactly_one_byte);
  RUN_TEST(test_parses_mono_64k);
  RUN_TEST(test_parses_320k_and_48khz);
  RUN_TEST(test_crc_flag_is_inverted);
  RUN_TEST(test_parses_mpeg2_at_half_rate);
  RUN_TEST(test_free_format_reports_unknown_length);
  RUN_TEST(test_rejects_reserved_and_invalid_encodings);
  RUN_TEST(test_mpeg2_5_parses_but_is_not_supported);
  RUN_TEST(test_supported_covers_mpeg1_mono_and_stereo);
  RUN_TEST(test_rejects_missing_sync_and_short_buffers);
  RUN_TEST(test_find_sync_skips_leading_garbage);
  RUN_TEST(test_find_sync_reports_failure_on_a_buffer_without_one);

  RUN_TEST(test_table1_decodes_all_four_codewords);
  RUN_TEST(test_table0_yields_zeros_without_consuming_bits);
  RUN_TEST(test_count1_table_b_is_four_inverted_bits);
  RUN_TEST(test_count1_table_a_shortest_codeword_is_all_zero);
  RUN_TEST(test_decode_fails_when_the_bits_run_out);
  RUN_TEST(test_every_used_table_is_a_complete_prefix_code);
  RUN_TEST(test_codes_are_sorted_within_each_length);
  RUN_TEST(test_unused_tables_are_absent);
  RUN_TEST(test_linbits_match_the_standard);

  RUN_TEST(test_mono_side_info_is_seventeen_bytes_and_round_trips);
  RUN_TEST(test_switched_granule_infers_region_counts);
  RUN_TEST(test_stereo_side_info_is_thirty_two_bytes);
  RUN_TEST(test_short_side_info_is_rejected);
  return UNITY_END();
}
