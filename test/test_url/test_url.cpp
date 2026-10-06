#include <unity.h>

#include <string>

#include "core/net/Url.h"

using namespace awtrix;

namespace {

void test_url_components_and_request_target() {
  const auto url = net::parseUrl("HTTPS://Api.Example:8443/a/b?q=one#section");
  TEST_ASSERT_TRUE(url.has_value());
  TEST_ASSERT_TRUE(url->scheme == "https" && url->host == "Api.Example" && url->port == "8443");
  TEST_ASSERT_EQUAL_INT(8443, url->effectivePort);
  TEST_ASSERT_EQUAL_STRING("https://Api.Example:8443", url->origin().c_str());
  TEST_ASSERT_EQUAL_STRING("/a/b?q=one", url->requestTarget().c_str());
  TEST_ASSERT_TRUE(url->fragment && *url->fragment == "section");
  TEST_ASSERT_FALSE(url->originOnly());
  TEST_ASSERT_FALSE(url->userinfo.has_value());
}

void test_user_information_does_not_become_the_host() {
  const auto url = net::parseUrl("https://api.example.com:443@evil.com/");
  TEST_ASSERT_TRUE(url.has_value());
  TEST_ASSERT_TRUE(url->host == "evil.com");
  TEST_ASSERT_TRUE(url->userinfo && *url->userinfo == "api.example.com:443");
  TEST_ASSERT_EQUAL_STRING("https://evil.com", url->origin().c_str());
  const auto emptyUser = net::parseUrl("https://@api.example.com/");
  TEST_ASSERT_TRUE(emptyUser && emptyUser->userinfo && emptyUser->userinfo->empty());
}

void test_ipv6_and_ports() {
  const auto url = net::parseUrl("https://[2001:db8::1]:8123/api");
  TEST_ASSERT_TRUE(url && url->host == "[2001:db8::1]" && url->effectivePort == 8123);
  TEST_ASSERT_EQUAL_STRING("https://[2001:db8::1]:8123", url->origin().c_str());
  for (const char* text : {"http://[::]", "http://[::1]", "http://[1:2:3:4:5:6:7:8]",
                           "http://[::ffff:192.0.2.1]", "http://[1:2:3:4:5:6:192.0.2.1]"})
    TEST_ASSERT_TRUE_MESSAGE(net::parseUrl(text).has_value(), text);
  TEST_ASSERT_EQUAL_INT(80, net::parseUrl("http://host")->effectivePort);
  TEST_ASSERT_EQUAL_INT(443, net::parseUrl("https://host")->effectivePort);
  TEST_ASSERT_EQUAL_INT(65535, net::parseUrl("http://host:65535")->effectivePort);
}

void test_empty_path_query_and_fragment() {
  TEST_ASSERT_EQUAL_STRING("/", net::parseUrl("http://host")->requestTarget().c_str());
  TEST_ASSERT_EQUAL_STRING("/?x=1", net::parseUrl("http://host?x=1#part")->requestTarget().c_str());
  TEST_ASSERT_EQUAL_STRING("/", net::parseUrl("http://host#part")->requestTarget().c_str());
  TEST_ASSERT_TRUE(net::parseUrl("http://host")->originOnly());
  TEST_ASSERT_FALSE(net::parseUrl("http://host/")->originOnly());
  TEST_ASSERT_FALSE(net::parseUrl("http://host?")->originOnly());
  TEST_ASSERT_FALSE(net::parseUrl("http://host#")->originOnly());
}

void test_malformed_authorities_are_refused() {
  for (const char* text : {"", "host/path", "//host/path", "ftp://host/", "http://", "http:///path",
                           "http://?q", "https://#fragment", "http://host:", "http://host:0",
                           "http://host:65536", "http://host:-1", "http://host:abc", "http://host:1:2",
                           "http://[]", "http://[::1", "http://[::1]extra", "http://[1:2]",
                           "http://[1::2::3]", "http://[1:2:3:4:5:6:7:8::]", "http://[12345::]",
                           "http://[::ffff:192.0.2.256]", "http://[::ffff:192.0.2.]",
                           "http://[1:2:3:4:5:6:7:192.0.2.1]", "http://2001:db8::1/",
                           "http://user@@host/", "http://host name/", "http://host\r\nX:bad/",
                           "http://host\\@other/", "http://%61pi.example/"})
    TEST_ASSERT_FALSE_MESSAGE(net::parseUrl(text).has_value(), text);
  const std::string nul("https://host/\0hidden", 20);
  TEST_ASSERT_FALSE(net::parseUrl(nul).has_value());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_url_components_and_request_target);
  RUN_TEST(test_user_information_does_not_become_the_host);
  RUN_TEST(test_ipv6_and_ports);
  RUN_TEST(test_empty_path_query_and_fragment);
  RUN_TEST(test_malformed_authorities_are_refused);
  return UNITY_END();
}
