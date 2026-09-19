#include <unity.h>

#include <cstdarg>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define AWTRIX_VFS_ROOT ".pio/native-tests/vfsroot"

#include "../../src/persistence/VfsFile.cpp"
#include "../../src/media/AssetFileDevice.cpp"

namespace awtrix {
void logf(const char*, ...) {}
}

namespace {

namespace stdfs = std::filesystem;
using awtrix::media::PodBuffer;
using awtrix::media::readAsset;

const stdfs::path kRoot = AWTRIX_VFS_ROOT;

void put(const std::string& path, const std::vector<uint8_t>& bytes) {
  const stdfs::path full = kRoot / path.substr(1);
  stdfs::create_directories(full.parent_path());
  std::ofstream out(full, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

const std::vector<uint8_t> kBinary = {'G', 'I', 'F', 0x0D, 0x0A, 0x1A, 0x00, 0xFF, 0x0A, 'x'};

}

void setUp() {
  stdfs::remove_all(kRoot);
  stdfs::create_directories(kRoot / "ICONS");
}
void tearDown() { stdfs::remove_all(kRoot); }

void test_read_asset_returns_the_exact_bytes() {
  put("/ICONS/a.gif", kBinary);
  PodBuffer<uint8_t> out;
  bool oom = true;
  TEST_ASSERT_TRUE(readAsset("/ICONS/a.gif", out, &oom));
  TEST_ASSERT_FALSE(oom);
  TEST_ASSERT_EQUAL_UINT32(kBinary.size(), out.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kBinary.data(), out.data(), kBinary.size());
}

void test_read_asset_resets_the_flag_for_a_missing_file() {
  PodBuffer<uint8_t> out;
  bool oom = true;
  TEST_ASSERT_FALSE(readAsset("/ICONS/none.gif", out, &oom));
  TEST_ASSERT_FALSE(oom);
  TEST_ASSERT_TRUE(out.empty());
  TEST_ASSERT_FALSE(readAsset("/ICONS/none.gif", out));
}

void test_read_asset_treats_an_empty_file_as_absent() {
  put("/ICONS/empty.gif", {});
  PodBuffer<uint8_t> out;
  bool oom = true;
  TEST_ASSERT_FALSE(readAsset("/ICONS/empty.gif", out, &oom));
  TEST_ASSERT_FALSE(oom);
}

void test_read_asset_refuses_an_overlong_path() {
  PodBuffer<uint8_t> out;
  bool oom = true;
  TEST_ASSERT_FALSE(readAsset("/ICONS/" + std::string(300, 'a') + ".gif", out, &oom));
  TEST_ASSERT_FALSE(oom);
}

void test_file_size_and_is_file() {
  put("/ICONS/a.gif", kBinary);
  put("/ICONS/empty.gif", {});
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kBinary.size()), static_cast<int>(awtrix::fs::fileSize("/ICONS/a.gif")));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(awtrix::fs::fileSize("/ICONS/empty.gif")));
  TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(awtrix::fs::fileSize("/ICONS/none.gif")));
  TEST_ASSERT_TRUE(awtrix::fs::isFile("/ICONS/a.gif"));
  TEST_ASSERT_TRUE(awtrix::fs::isFile("/ICONS/empty.gif"));
  TEST_ASSERT_FALSE(awtrix::fs::isFile("/ICONS/none.gif"));
  TEST_ASSERT_FALSE(awtrix::fs::isFile("/ICONS/" + std::string(300, 'a')));
}

void test_vfs_path_prefixes_the_mount_point() {
  TEST_ASSERT_EQUAL_STRING(AWTRIX_VFS_ROOT "/ICONS", awtrix::fs::vfsPath("/ICONS").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_read_asset_returns_the_exact_bytes);
  RUN_TEST(test_read_asset_resets_the_flag_for_a_missing_file);
  RUN_TEST(test_read_asset_treats_an_empty_file_as_absent);
  RUN_TEST(test_read_asset_refuses_an_overlong_path);
  RUN_TEST(test_file_size_and_is_file);
  RUN_TEST(test_vfs_path_prefixes_the_mount_point);
  return UNITY_END();
}
