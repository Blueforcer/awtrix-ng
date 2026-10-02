#include <unity.h>
#include "hal/GalacticUnicornDisplay.h"
using namespace awtrix;
using namespace awtrix::galactic;
void setUp() {}
void tearDown() {}
static uint16_t decode(const Stream& s, int x, int y, int bit) {
  uint16_t value = 0;
  for (int p = 0; p < Planes; ++p)
    value |= ((s.bytes[(10-y)*RowBytes + p*PlaneBytes + 2 + 52-x] >> bit) & 1u) << p;
  return value;
}
static void stream_format() {
  Stream s;
  clearStream(s);
  TEST_ASSERT_EQUAL(9240, sizeof(s));
  for (int row = 0; row < 11; ++row) for (int p = 0; p < 14; ++p) {
    auto* b = s.bytes.data() + row*RowBytes + p*PlaneBytes;
    TEST_ASSERT_EQUAL(52, b[0]);
    TEST_ASSERT_EQUAL(row, b[1]);
    for (int i = 2; i < 56; ++i) TEST_ASSERT_EQUAL(0, b[i]);
    const uint32_t ticks = b[56] | (b[57]<<8) | (b[58]<<16) | (b[59]<<24);
    TEST_ASSERT_EQUAL_UINT32(1u << p, ticks);
  }
}
static void bits_and_orientation() {
  Stream s; clearStream(s);
  packPixel(s, 0, 0, 0x1234, 0x2345, 0x3456);
  TEST_ASSERT_EQUAL_HEX16(0x1234, decode(s,0,0,2));
  TEST_ASSERT_EQUAL_HEX16(0x2345, decode(s,0,0,1));
  TEST_ASSERT_EQUAL_HEX16(0x3456, decode(s,0,0,0));
  for (int p = 0; p < 14; ++p) {
    packPixel(s,52,10,1u<<p,0,0);
    for (int q = 0; q < 14; ++q) TEST_ASSERT_EQUAL(q == p ? 4 : 0, s.bytes[q*PlaneBytes+2]);
  }
  const auto before = s.bytes;
  packPixel(s,-1,0,16383,16383,16383);
  packPixel(s,53,0,16383,16383,16383);
  packPixel(s,0,11,16383,16383,16383);
  TEST_ASSERT_TRUE(before == s.bytes);
  packPixel(s,0,0,0,0,0);
  TEST_ASSERT_EQUAL(0, decode(s,0,0,2));
}
static void heights_and_letterbox() {
  TEST_ASSERT_EQUAL(8,sanitizeHeight(8));
  for (int h : {0,7,9,10,11,12,16}) TEST_ASSERT_EQUAL(11,sanitizeHeight(h));
  TEST_ASSERT_EQUAL(-1,physicalRow(-1,8));
  TEST_ASSERT_EQUAL(-1,physicalRow(8,8));
  TEST_ASSERT_EQUAL(-1,physicalRow(11,11));
  Stream s; Grade14 grade;
  Canvas c(53,11); c.clear(0xffffff);
  for (int h : {11,8,11}) {
    packCanvas(s,c,h,grade);
    for (int y = 0; y < 11; ++y) for (int x = 0; x < 53; ++x) {
      const bool dark = h == 8 && (y == 0 || y >= 9);
      TEST_ASSERT_EQUAL(dark ? 0 : 16383,decode(s,x,y,2));
    }
    for (int y = 0; y < h; ++y) TEST_ASSERT_EQUAL(y+(h==8?1:0),physicalRow(y,h));
  }
  Canvas small(1,1); small.setPixel(0,0,0xffffff);
  packCanvas(s,small,8,grade);
  TEST_ASSERT_EQUAL(16383,decode(s,0,1,2));
  TEST_ASSERT_EQUAL(0,decode(s,1,1,2));
  TEST_ASSERT_EQUAL(0,decode(s,0,2,2));
}
static void mirror_and_rotate() {
  Stream s; Grade14 grade;
  Canvas c(53,11); c.clear(0); c.setPixel(0,0,0xffffff);
  const struct { Orientation o; int x, y; } cases[] = {
      {{false,false},0,0}, {{true,false},52,0}, {{false,true},52,10}, {{true,true},0,10}};
  for (const auto& k : cases) {
    packCanvas(s,c,11,grade,k.o);
    TEST_ASSERT_EQUAL(16383,decode(s,k.x,k.y,2));
    if (k.x || k.y) TEST_ASSERT_EQUAL(0,decode(s,0,0,2));
  }
  Canvas band(53,8); band.clear(0); band.setPixel(0,0,0xffffff);
  packCanvas(s,band,8,grade,{false,true}); // Rotated inside the letterbox, rows 1-8.
  TEST_ASSERT_EQUAL(16383,decode(s,52,8,2));
  TEST_ASSERT_EQUAL(0,decode(s,52,10,2));
}
static void gamma_brightness_grade() {
  Grade14 g; render::GradeParams p; p.gamma = 2.2f; g.setParams(p);
  TEST_ASSERT_EQUAL(0,g.channel(0,0));
  TEST_ASSERT_EQUAL(16383,g.channel(0,255));
  TEST_ASSERT_EQUAL(3596,g.channel(0,128)); // Pimoroni v1.23.0 GAMMA_14BIT[128]
  for(int i=1;i<256;++i) TEST_ASSERT_TRUE(g.channel(0,i) >= g.channel(0,i-1));
  p.brightness=128; g.setParams(p);
  TEST_ASSERT_EQUAL(16383u*128u/255u,g.channel(0,255)); // linear light, not gamma twice
  p.brightness=0; g.setParams(p);
  TEST_ASSERT_EQUAL(0,g.channel(0,255));
  p.brightness=255; p.gamma=1; p.correction=0xff8000; p.tint=0x80ffff; g.setParams(p);
  TEST_ASSERT_EQUAL(16383u*128u/255u,g.channel(0,255));
  TEST_ASSERT_EQUAL(16383u*128u/255u,g.channel(1,255));
  TEST_ASSERT_EQUAL(0,g.channel(2,255));
  p.correction=p.tint=0xffffff; p.saturation=0; g.setParams(p);
  Stream s; clearStream(s); g.pack(s,0,0,0xff0000);
  TEST_ASSERT_EQUAL(decode(s,0,0,2),decode(s,0,0,1));
  TEST_ASSERT_EQUAL(decode(s,0,0,2),decode(s,0,0,0));
  // Same NG curve at higher precision: rounded down to bytes within one level.
  p.saturation=100; p.gamma=1.9f; p.brightness=120; g.setParams(p);
  render::ColorGrade reference; reference.setParams(p);
  for(int i=0;i<256;++i) {
    const uint32_t rgb = i*0x010101u;
    TEST_ASSERT_INT_WITHIN(1,color::red(reference.applyPixel(rgb)),g.channel(0,i)*255u/16383u);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(stream_format);
  RUN_TEST(bits_and_orientation);
  RUN_TEST(heights_and_letterbox);
  RUN_TEST(mirror_and_rotate);
  RUN_TEST(gamma_brightness_grade);
  return UNITY_END();
}
