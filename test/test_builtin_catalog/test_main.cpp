#include <unity.h>
#include <iostream>
#include "core/BuiltinCatalog.h"
#include "core/api/CapabilitiesJson.h"
using namespace awtrix;
void setUp() {} void tearDown() {}
void builtin_catalog_parity() {
  BuiltinCatalog esp, pico; AppRegistry ea, pa; EffectRegistry ef, pf, eo, po;
  esp.addTo(ea,ef,eo); pico.addTo(pa,pf,po);
  TEST_ASSERT_EQUAL(5,pa.size()); TEST_ASSERT_EQUAL(19,pf.size()); TEST_ASSERT_EQUAL(6,po.size());
  TEST_ASSERT_TRUE(ef.names() == pf.names()); TEST_ASSERT_TRUE(ef.paletteNames() == pf.paletteNames());
  TEST_ASSERT_TRUE(eo.names() == po.names());
  const std::vector<std::string> expected = {"BrickBreaker","Checkerboard","ColorWaves","Fade",
    "Fireworks","LookingEyes","Matrix","MovingLine","Pacifica","PingPong","Plasma","PlasmaCloud",
    "Radar","Ripple","Snake","SwirlIn","SwirlOut","TheaterChase","TwinklingStars"};
  TEST_ASSERT_TRUE(expected == pf.names()); TEST_ASSERT_EQUAL(15,pf.paletteNames().size());
  const std::vector<std::string> overlays = {"drizzle","frost","rain","snow","storm","thunder"};
  TEST_ASSERT_TRUE(overlays == po.names());
  std::cout << "ESP32/Pico built-ins equal: " << api::capabilitiesJson(pf.names(),pf.paletteNames(),po.names(), {}) << '\n';
}
int main() { UNITY_BEGIN(); RUN_TEST(builtin_catalog_parity); return UNITY_END(); }
