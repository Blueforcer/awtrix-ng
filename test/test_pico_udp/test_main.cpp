#include <unity.h>
#define AWTRIX_PLATFORM_RP2040
#include "transport/net/UdpSocket.cpp"
#include "transport/net/DiscoveryService.cpp"
#include "transport/net/ArtnetService.cpp"
void setUp() {
  WiFiUDP::incoming.clear(); WiFiUDP::sent.clear();
  WiFiUDP::consumed = false; WiFiUDP::bindOk = WiFiUDP::sendOk = true;
}
void tearDown() {}
static void socket_contract() {
  awtrix::UdpSocket s;
  char b[2];
  TEST_ASSERT_EQUAL_INT(-1, s.receive(b, 2));
  TEST_ASSERT_FALSE(s.replyTo(4211, "x", 1));
  TEST_ASSERT_TRUE(s.open(4210));
  WiFiUDP::incoming = {1,2,3,4};
  TEST_ASSERT_EQUAL_INT(2, s.receive(b, 2));
  TEST_ASSERT_EQUAL_INT(1, b[0]);
  TEST_ASSERT_EQUAL_INT(2, b[1]);
  TEST_ASSERT_EQUAL_INT(0, s.receive(b, 2));
  TEST_ASSERT_EQUAL_INT(4, WiFiUDP::pos);
  TEST_ASSERT_TRUE(s.replyTo(4211, "x", 1));
  TEST_ASSERT_EQUAL_INT(4211, WiFiUDP::destination);
  TEST_ASSERT_EQUAL_UINT32(WiFiUDP::peer, WiFiUDP::recipient);
  s.close();
  TEST_ASSERT_FALSE(s.replyTo(4211, "x", 1));
  WiFiUDP::bindOk = false;
  TEST_ASSERT_FALSE(s.open(4210));
}
static void discovery() {
  awtrix::DiscoveryService d;
  d.begin("awtrix-test", 8080);
  const char q[] = "FIND_AWTRIXNG";
  WiFiUDP::incoming.assign(q, q + sizeof(q));
  d.tick();
  const std::string reply(WiFiUDP::sent.begin(), WiFiUDP::sent.end());
  TEST_ASSERT_EQUAL_STRING("awtrix-test:8080", reply.c_str());
  TEST_ASSERT_EQUAL_INT(4211, WiFiUDP::destination);
}
static void artnet_frame_and_timeout() {
  awtrix::ArtnetService a;
  awtrix::Canvas c(53,11);
  a.begin();
  TEST_ASSERT_EQUAL_INT(6454, WiFiUDP::listen);
  WiFiUDP::incoming = {'A','r','t','-','N','e','t',0, 0,0x50, 0,14, 0,0, 0,0, 0,3, 255,0,0};
  TEST_ASSERT_TRUE(a.tick(c, 100));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0, c.getPixel(1, 0));
  WiFiUDP::incoming[14] = 1; // universe one starts at pixel 170, not 512
  WiFiUDP::incoming[18] = 0; WiFiUDP::incoming[19] = 255;
  WiFiUDP::consumed = false;
  TEST_ASSERT_TRUE(a.tick(c, 100));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00, c.getPixel(170 % 53, 170 / 53));
  TEST_ASSERT_TRUE(a.tick(c, 5099));
  TEST_ASSERT_FALSE(a.tick(c, 5100));
  a.end();
  TEST_ASSERT_FALSE(a.tick(c, 5101));
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(socket_contract);
  RUN_TEST(discovery);
  RUN_TEST(artnet_frame_and_timeout);
  return UNITY_END();
}
