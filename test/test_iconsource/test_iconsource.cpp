#include <unity.h>

#include <string>
#include <vector>

#include "core/icons/IconSource.h"
#include "core/payload/Base64.h"

using namespace awtrix;
using Kind = icons::Source::Kind;

void setUp() {}
void tearDown() {}

namespace {
const char* kTinyGif = "R0lGODlhCAAIAPAAAAAAAP///yH5BAkIAAAALAAAAAAIAAgAAAIHDI6py+3fCgA7";
}

void test_plain_text_is_a_file_name() {
  const auto source = icons::parse("weather_sun-2");
  TEST_ASSERT_TRUE(source.kind == Kind::kFile);
  TEST_ASSERT_EQUAL_STRING("weather_sun-2", std::string(source.value).c_str());
  TEST_ASSERT_TRUE(source.offers(icons::ImageFormat::kGif));
  TEST_ASSERT_TRUE(source.offers(icons::ImageFormat::kJpeg));
}

void test_file_names_are_bounded_and_cannot_leave_the_icon_folder() {
  TEST_ASSERT_TRUE(icons::parse(std::string(64, 'a')).file());
  const char* invalid[] = {"", "../config", "a/b", "a\\b", "a..b"};
  for (const char* name : invalid)
    TEST_ASSERT_TRUE_MESSAGE(icons::parse(name).kind == Kind::kInvalid, name);
  TEST_ASSERT_TRUE(icons::parse(std::string(65, 'a')).kind == Kind::kInvalid);
  TEST_ASSERT_TRUE(icons::parse(std::string("a\0b", 3)).kind == Kind::kInvalid);
}

void test_data_uri_declares_its_format() {
  const std::string gif = std::string("data:image/gif;base64,") + kTinyGif;
  const auto a = icons::parse(gif);
  TEST_ASSERT_TRUE(a.inlined());
  TEST_ASSERT_TRUE(a.format == icons::ImageFormat::kGif);
  TEST_ASSERT_EQUAL_STRING(kTinyGif, std::string(a.value).c_str());
  TEST_ASSERT_TRUE(a.offers(icons::ImageFormat::kGif));
  TEST_ASSERT_FALSE(a.offers(icons::ImageFormat::kJpeg));

  const auto b = icons::parse("data:image/jpeg;base64,/9j/4AAQ");
  TEST_ASSERT_TRUE(b.inlined());
  TEST_ASSERT_TRUE(b.format == icons::ImageFormat::kJpeg);
  TEST_ASSERT_FALSE(b.offers(icons::ImageFormat::kGif));
}

void test_data_uri_scheme_type_and_encoding_ignore_case() {
  const auto source = icons::parse(std::string("DATA:Image/GIF;BASE64,") + kTinyGif);
  TEST_ASSERT_TRUE(source.inlined());
  TEST_ASSERT_TRUE(source.format == icons::ImageFormat::kGif);
  TEST_ASSERT_EQUAL_STRING(kTinyGif, std::string(source.value).c_str());
}

void test_unsupported_data_uris_are_invalid() {
  const char* invalid[] = {
      "data:image/png;base64,iVBORw0KGgo=",
      "data:image/jpg;base64,/9j/4AAQ",
      "data:image/gif,GIF89a",
      "data:image/gif;charset=utf-8;base64,R0lGODlh",
      "data:image/gif;base64,",
      "data:,",
  };
  for (const char* uri : invalid)
    TEST_ASSERT_TRUE_MESSAGE(icons::parse(uri).kind == Kind::kInvalid, uri);
}

void test_data_uri_length_is_bounded() {
  const std::string prefix = "data:image/gif;base64,";
  std::string fits = prefix + std::string(icons::kMaxInlineBytes - prefix.size(), 'A');
  TEST_ASSERT_TRUE(icons::parse(fits).inlined());
  TEST_ASSERT_TRUE(icons::parse(fits + "A").kind == Kind::kInvalid);
}

void test_valid_checks_the_base64_payload() {
  TEST_ASSERT_TRUE(icons::valid(std::string("data:image/gif;base64,") + kTinyGif));
  TEST_ASSERT_TRUE(icons::valid("data:image/gif;base64,R0lGODlh"));
  TEST_ASSERT_TRUE(icons::valid("data:image/gif;base64,R0lGODlhAQ=="));
  TEST_ASSERT_FALSE(icons::valid("data:image/gif;base64,R0lG OD"));
  TEST_ASSERT_FALSE(icons::valid("data:image/gif;base64,R0l=GOD"));
  TEST_ASSERT_FALSE(icons::valid("data:image/gif;base64,A"));
  TEST_ASSERT_FALSE(icons::valid("data:image/gif;base64,===="));
  TEST_ASSERT_TRUE(icons::valid("1234"));
  TEST_ASSERT_FALSE(icons::valid(""));
}

void test_raw_base64_is_not_an_icon() {
  const std::string raw =
      "R0lGODlhCAAIALMAAARu/By+LPyiBPwCBNT+BIQC/ATC/PzGBHz+BPyqBPxGBPz2BPwC/AAAAAAAAAAAACH";
  TEST_ASSERT_TRUE(raw.size() > 64);
  TEST_ASSERT_FALSE(icons::valid(raw));
  TEST_ASSERT_FALSE(icons::valid(kTinyGif));
}

void test_base64_decodes_into_a_caller_buffer() {
  const char* text = "R0lGODlh";
  TEST_ASSERT_EQUAL_UINT(6, base64::decodedSize(text, 8));
  uint8_t out[6] = {};
  std::size_t written = 0;
  TEST_ASSERT_TRUE(base64::decode(text, 8, out, written));
  TEST_ASSERT_EQUAL_UINT(6, written);
  TEST_ASSERT_EQUAL_MEMORY("GIF89a", out, 6);

  TEST_ASSERT_EQUAL_UINT(1, base64::decodedSize("QQ==", 4));
  TEST_ASSERT_TRUE(base64::decode("QQ==", 4, out, written));
  TEST_ASSERT_EQUAL_UINT(1, written);
  TEST_ASSERT_EQUAL_UINT8('A', out[0]);

  TEST_ASSERT_FALSE(base64::decode("QQ*=", 4, out, written));
  TEST_ASSERT_EQUAL_UINT(0, written);

  std::vector<uint8_t> vec;
  TEST_ASSERT_TRUE(base64::decode(text, 8, vec));
  TEST_ASSERT_EQUAL_UINT(6, vec.size());
}

void test_http_and_https_urls_are_remote_icons() {
  const char* urls[] = {
      "https://example.com/cover.jpg",
      "http://192.168.1.5:8123/api/media_player_proxy/media_player.kitchen?token=a&cache=b",
      "HTTPS://Example.com/a%20b.png#frag",
      "http://host",
  };
  for (const char* url : urls) {
    const auto source = icons::parse(url);
    TEST_ASSERT_TRUE_MESSAGE(source.remote(), url);
    TEST_ASSERT_EQUAL_STRING(url, std::string(source.value).c_str());
    TEST_ASSERT_FALSE(source.offers(icons::ImageFormat::kGif));
    TEST_ASSERT_FALSE(source.offers(icons::ImageFormat::kJpeg));
    TEST_ASSERT_TRUE_MESSAGE(icons::valid(url), url);
  }
}

void test_malformed_urls_are_invalid() {
  const char* invalid[] = {
      "http://",
      "https:///path",
      "http://?q",
      "http://#f",
      "https://example.com/a b.jpg",
      "https://example.com/\xc3\xa4.jpg",
      "https://example.com/\ttab",
      "ftp://example.com/a.jpg",
      "https:/example.com/a.jpg",
  };
  for (const char* url : invalid) {
    TEST_ASSERT_FALSE_MESSAGE(icons::parse(url).remote(), url);
    TEST_ASSERT_FALSE_MESSAGE(icons::valid(url), url);
  }
}

void test_url_length_is_bounded() {
  const std::string prefix = "https://example.com/";
  const std::string fits = prefix + std::string(icons::kMaxUrlBytes - prefix.size(), 'a');
  TEST_ASSERT_TRUE(icons::parse(fits).remote());
  TEST_ASSERT_TRUE(icons::parse(fits + "a").kind == Kind::kInvalid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_plain_text_is_a_file_name);
  RUN_TEST(test_file_names_are_bounded_and_cannot_leave_the_icon_folder);
  RUN_TEST(test_data_uri_declares_its_format);
  RUN_TEST(test_data_uri_scheme_type_and_encoding_ignore_case);
  RUN_TEST(test_unsupported_data_uris_are_invalid);
  RUN_TEST(test_data_uri_length_is_bounded);
  RUN_TEST(test_valid_checks_the_base64_payload);
  RUN_TEST(test_raw_base64_is_not_an_icon);
  RUN_TEST(test_base64_decodes_into_a_caller_buffer);
  RUN_TEST(test_http_and_https_urls_are_remote_icons);
  RUN_TEST(test_malformed_urls_are_invalid);
  RUN_TEST(test_url_length_is_bounded);
  return UNITY_END();
}
