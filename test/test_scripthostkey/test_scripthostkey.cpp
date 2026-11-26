#include <unity.h>

#include <cstring>
#include <string>

#include "core/remote/PairingKey.h"
#include "core/remote/ScriptHostStatus.h"

using namespace awtrix;
using namespace awtrix::remote;

namespace {
int calls = 0;
bool fakeHmac(const uint8_t* key, std::size_t keyLength, const uint8_t* data, std::size_t length,
              uint8_t* out) {
  ++calls;
  uint8_t sum = 0;
  for (std::size_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum * 31 + data[i]);
  for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(key[i % keyLength] ^ (length + i) ^ sum);
  return true;
}
const std::string kHex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
}

void setUp() { calls = 0; }
void tearDown() {}

static void test_a_key_is_read_from_hex() {
  PairingKey key;
  TEST_ASSERT_TRUE(PairingKey::parse(kHex, key));
  TEST_ASSERT_EQUAL_HEX8(0x00, key.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x1f, key.bytes[31]);
  TEST_ASSERT_FALSE(PairingKey::parse("", key));
  TEST_ASSERT_FALSE(PairingKey::parse(kHex.substr(1), key));
  TEST_ASSERT_FALSE(PairingKey::parse(std::string(64, 'G'), key));
}

static void test_the_signer_and_the_check_agree() {
  PairingKey key;
  PairingKey::parse(kHex, key);
  wire::Hello hello;
  hello.uid = "a1b2c3";
  hello.width = 32;
  hello.height = 8;
  hello.address = 0xC0A8B214u;
  hello.webPort = 80;
  uint8_t buf[wire::kMaxDatagram];
  const std::size_t n = wire::encodeHello(hello, signerFor(key, fakeHmac), buf, sizeof(buf));
  wire::Packet p;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  TEST_ASSERT_TRUE(signedBy(p, key, fakeHmac));
  buf[7] ^= 1;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  TEST_ASSERT_FALSE_MESSAGE(signedBy(p, key, fakeHmac), "a changed byte breaks the tag");
  PairingKey other;
  PairingKey::parse(std::string(64, 'a'), other);
  buf[7] ^= 1;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  TEST_ASSERT_FALSE_MESSAGE(signedBy(p, other, fakeHmac), "another key does not fit");
  const std::size_t addressAt = 6 + 1 + hello.uid.size() + 2 + 2;
  buf[addressAt] ^= 1;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  TEST_ASSERT_EQUAL_HEX32(0xC0A8B215u, p.hello.address);
  TEST_ASSERT_FALSE_MESSAGE(signedBy(p, key, fakeHmac), "the clock's address is signed");
}

static void test_a_signed_offer_is_checked_like_a_hello() {
  PairingKey key;
  PairingKey::parse(kHex, key);
  wire::Offer offer;
  offer.name = "pi";
  offer.port = 8301;
  offer.uid = "a1b2c3";
  offer.unixTime = 1790000000u;
  uint8_t buf[wire::kMaxDatagram];
  const std::size_t n = wire::encodeOffer(offer, buf, sizeof(buf), signerFor(key, fakeHmac));
  wire::Packet p;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  TEST_ASSERT_TRUE(signedBy(p, key, fakeHmac));
  buf[n - wire::kTagBytes - 1] ^= 1;
  TEST_ASSERT_TRUE(wire::decode(buf, n, p));
  TEST_ASSERT_FALSE_MESSAGE(signedBy(p, key, fakeHmac), "a changed time breaks the tag");
  offer.uid = std::string_view();
  TEST_ASSERT_TRUE(wire::decode(buf, wire::encodeOffer(offer, buf, sizeof(buf)), p));
  TEST_ASSERT_FALSE_MESSAGE(signedBy(p, key, fakeHmac), "a plain offer carries no tag");
}

static void test_the_host_spec_splits_into_name_and_port() {
  std::string name;
  uint16_t port = 0;
  TEST_ASSERT_TRUE(splitHost("raspberrypi.local:8302", name, port));
  TEST_ASSERT_EQUAL_STRING("raspberrypi.local", name.c_str());
  TEST_ASSERT_EQUAL(8302, port);
  TEST_ASSERT_TRUE(splitHost("192.168.1.5", name, port));
  TEST_ASSERT_EQUAL(kDefaultClockPort, port);
  TEST_ASSERT_FALSE(splitHost("", name, port));
  TEST_ASSERT_FALSE(splitHost("pi:", name, port));
  TEST_ASSERT_FALSE(splitHost("pi:99999", name, port));
  TEST_ASSERT_FALSE(splitHost("pi:0", name, port));
  TEST_ASSERT_FALSE(splitHost("pi:8a", name, port));
  TEST_ASSERT_EQUAL_STRING("192.168.1.5:8302", hostSpec(0xC0A80105u, 8302).c_str());
}

static void test_the_status_lists_mode_link_and_offers() {
  ScriptHostStatus status;
  status.host = "pi:8301";
  status.link = "up";
  OfferEntry offer;
  offer.name = "pi";
  offer.address = 0xC0A80105u;
  offer.port = 8300;
  status.offers.push_back(offer);
  TEST_ASSERT_EQUAL_STRING(
      "{\"mode\":\"remote\",\"host\":\"pi:8301\",\"link\":\"up\","
      "\"offers\":[{\"name\":\"pi\",\"address\":\"192.168.1.5\",\"port\":8300}]}",
      scriptHostJson(status).c_str());
  status.host.clear();
  status.link = "off";
  status.offers.clear();
  TEST_ASSERT_EQUAL_STRING("{\"mode\":\"off\",\"host\":\"\",\"link\":\"off\",\"offers\":[]}",
                           scriptHostJson(status).c_str());
}

static void test_the_key_is_told_only_while_paired() {
  ScriptHostStatus status;
  status.host = "pi:8301";
  status.link = "up";
  TEST_ASSERT_NULL_MESSAGE(strstr(scriptHostJson(status).c_str(), "key"), "not unless asked for");
  status.key = "ab12";
  TEST_ASSERT_EQUAL_STRING("{\"mode\":\"remote\",\"host\":\"pi:8301\",\"link\":\"up\",\"key\":\"ab12\","
                           "\"offers\":[]}",
                           scriptHostJson(status).c_str());
  status.host.clear();
  TEST_ASSERT_NULL_MESSAGE(strstr(scriptHostJson(status).c_str(), "key"), "an old key of an unpaired clock stays");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_key_is_read_from_hex);
  RUN_TEST(test_the_signer_and_the_check_agree);
  RUN_TEST(test_a_signed_offer_is_checked_like_a_hello);
  RUN_TEST(test_the_host_spec_splits_into_name_and_port);
  RUN_TEST(test_the_status_lists_mode_link_and_offers);
  RUN_TEST(test_the_key_is_told_only_while_paired);
  return UNITY_END();
}
