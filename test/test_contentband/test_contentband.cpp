#include <unity.h>

#include "core/apps/builtin/BatteryApp.h"
#include "core/apps/builtin/DateApp.h"
#include "core/apps/builtin/HumidityApp.h"
#include "core/apps/builtin/TempApp.h"
#include "core/apps/builtin/TimeApp.h"
#include "core/render/ContentBand.h"
#include "core/render/ProvisioningScreen.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_band_view_bounds_and_placement() {
  for (int h : {8, 9, 10, 11, 16}) {
    const int top = (h - 8) / 2;
    TEST_ASSERT_EQUAL_INT(top, render::contentBandTop(h));
    std::vector<uint32_t> storage(53 * h + 2, 0xDEADBEEFu);
    Canvas panel(53, h, storage.data() + 1);
    panel.clear();
    Canvas band = render::contentBand(panel);
    TEST_ASSERT_EQUAL_INT(53, band.width());
    TEST_ASSERT_EQUAL_INT(8, band.height());
    band.clear(0x123456u);
    band.setPixel(-1, 0, 1);
    band.setPixel(53, 7, 1);
    band.setPixel(0, -1, 1);
    band.setPixel(0, 8, 1);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < 53; ++x)
        TEST_ASSERT_EQUAL_HEX32(y >= top && y < top + 8 ? 0x123456u : 0, panel.getPixel(x, y));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, storage.front());
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, storage.back());
  }
  Canvas empty(0, 0);
  Canvas view = render::contentBand(empty);
  TEST_ASSERT_EQUAL_INT(0, view.size());
}

static void assertShifted(const Canvas& shortPanel, const Canvas& tallPanel) {
  const int top = render::contentBandTop(tallPanel.height());
  for (int y = 0; y < tallPanel.height(); ++y)
    for (int x = 0; x < tallPanel.width(); ++x)
      TEST_ASSERT_EQUAL_HEX32(shortPanel.getPixel(x, y - top), tallPanel.getPixel(x, y));
}

// Exercise actual bundled fonts, including descenders and their original clipping.
#include "media/AwtrixFontAdapter.h"

static void test_builtin_modes_and_provisioning_use_band() {
  Settings settings;
  RuntimeState runtime;
  RenderCtx ctx;
  ctx.settings = &settings;
  ctx.runtime = &runtime;
  ctx.hour = 12; ctx.minute = 34; ctx.second = 56;
  ctx.mday = 23; ctx.month = 9; ctx.year = 2026; ctx.weekday = 3;
  TimeApp time;
  DateApp date;
  TempApp temp;
  HumidityApp humidity;
  BatteryApp battery;
  for (int height : {11, 16}) {
    for (int mode = 0; mode <= 6; ++mode) {
      settings.timeMode = mode;
      settings.weekdayBar.show = true;
      for (IApp* app : {static_cast<IApp*>(&time), static_cast<IApp*>(&date),
                        static_cast<IApp*>(&temp), static_cast<IApp*>(&humidity),
                        static_cast<IApp*>(&battery)}) {
        Canvas small(53, 8), tall(53, height);
        for (FontId id : {FontId::Small, FontId::Large}) {
          small.clear();
          tall.clear();
          ctx.font = &awtrixFont(id);
          app->render(small, ctx);
          app->render(tall, ctx);
          assertShifted(small, tall);
          render::drawProvisioningScreen(small, *ctx.font, 1000);
          render::drawProvisioningScreen(tall, *ctx.font, 1000);
          assertShifted(small, tall);
        }
      }
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_band_view_bounds_and_placement);
  RUN_TEST(test_builtin_modes_and_provisioning_use_band);
  return UNITY_END();
}
