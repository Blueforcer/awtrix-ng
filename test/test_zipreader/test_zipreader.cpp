#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/backup/ZipReader.h"
#include "zip_fixture.h"

using namespace awtrix;

namespace {

struct Recorder : backup::ZipVisitor {
  struct Entry {
    std::string name;
    uint32_t declaredSize = 0;
    std::string data;
    bool crcOk = false;
    bool ended = false;
  };
  std::vector<Entry> entries;
  bool archiveEnded = false;

  void onEntryStart(const std::string& name, uint32_t size) override {
    Entry e;
    e.name = name;
    e.declaredSize = size;
    entries.push_back(e);
  }
  void onEntryData(const uint8_t* data, std::size_t n) override {
    entries.back().data.append(reinterpret_cast<const char*>(data), n);
  }
  void onEntryEnd(bool crcOk) override {
    entries.back().crcOk = crcOk;
    entries.back().ended = true;
  }
  void onArchiveEnd() override { archiveEnded = true; }
};

Recorder readWhole() {
  Recorder rec;
  backup::ZipReader r(rec);
  r.feed(awtrix_test::kZip, awtrix_test::kZipLen);
  r.finish();
  return rec;
}

Recorder readByteByByte() {
  Recorder rec;
  backup::ZipReader r(rec);
  for (unsigned i = 0; i < awtrix_test::kZipLen; ++i) r.feed(&awtrix_test::kZip[i], 1);
  r.finish();
  return rec;
}

void expectFixture(const Recorder& rec) {
  TEST_ASSERT_TRUE(rec.archiveEnded);
  TEST_ASSERT_EQUAL_UINT(4, rec.entries.size());

  TEST_ASSERT_EQUAL_STRING("manifest.json", rec.entries[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"app\":\"awtrix-ng\",\"backupFormat\":1}", rec.entries[0].data.c_str());
  TEST_ASSERT_EQUAL_UINT(36, rec.entries[0].declaredSize);
  TEST_ASSERT_TRUE(rec.entries[0].ended);
  TEST_ASSERT_TRUE(rec.entries[0].crcOk);

  TEST_ASSERT_EQUAL_STRING("ICONS/a.gif", rec.entries[1].name.c_str());
  TEST_ASSERT_EQUAL_UINT(11, rec.entries[1].data.size());
  TEST_ASSERT_EQUAL_HEX8('G', rec.entries[1].data[0]);
  TEST_ASSERT_TRUE(rec.entries[1].crcOk);

  TEST_ASSERT_EQUAL_STRING("PALETTES/empty.txt", rec.entries[2].name.c_str());
  TEST_ASSERT_EQUAL_UINT(0, rec.entries[2].data.size());
  TEST_ASSERT_TRUE(rec.entries[2].ended);
  TEST_ASSERT_TRUE(rec.entries[2].crcOk);

  TEST_ASSERT_EQUAL_STRING("MELODIES/song.txt", rec.entries[3].name.c_str());
  TEST_ASSERT_EQUAL_STRING("bell:d=4,o=5,b=100:e,c", rec.entries[3].data.c_str());
  TEST_ASSERT_TRUE(rec.entries[3].crcOk);
}

void test_reads_all_entries_in_one_chunk() {
  const Recorder rec = readWhole();
  expectFixture(rec);
}

void test_reads_all_entries_byte_by_byte() {
  const Recorder rec = readByteByByte();
  expectFixture(rec);
}

void test_detects_crc_mismatch() {
  std::vector<uint8_t> bytes(awtrix_test::kZip, awtrix_test::kZip + awtrix_test::kZipLen);
  bytes[43] ^= 0xFF;
  Recorder rec;
  backup::ZipReader r(rec);
  r.feed(bytes.data(), bytes.size());
  r.finish();
  TEST_ASSERT_TRUE(rec.entries.size() >= 1);
  TEST_ASSERT_FALSE(rec.entries[0].crcOk);
}

void test_rejects_garbage() {
  const uint8_t junk[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  Recorder rec;
  backup::ZipReader r(rec);
  r.feed(junk, sizeof(junk));
  const bool finished = r.finish();
  TEST_ASSERT_FALSE(r.ok());
  TEST_ASSERT_FALSE(finished);
  TEST_ASSERT_EQUAL_UINT(0, rec.entries.size());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reads_all_entries_in_one_chunk);
  RUN_TEST(test_reads_all_entries_byte_by_byte);
  RUN_TEST(test_detects_crc_mismatch);
  RUN_TEST(test_rejects_garbage);
  UNITY_END();
  return 0;
}
