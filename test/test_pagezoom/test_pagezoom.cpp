#include <unity.h>

#include "core/Settings.h"
#include "platform/linux/render/PageZoom.h"

using namespace awtrix;

namespace {

struct FakeImages : IPageIcon {
  mutable int probes = 0;
  IconLoad begin(const std::string&, int, int) override { return IconLoad::kMissing; }
  void clear() override {}
  void advance(int64_t) override {}
  void blit(Canvas&, int, int) const override {}
  int width() const override { return 0; }
  bool nativeInfo(std::string_view id, int maxWidth, int maxHeight, int& width, int& height,
                  std::size_t& bytes) const override {
    ++probes;
    bytes = 0;
    if (id == "small") width = height = 8;
    else if (id == "big") width = height = 16;
    else return false;
    return width <= maxWidth && height <= maxHeight;
  }
};

AppSpec page(const std::string& icon, const std::string& placed = "") {
  AppSpec spec;
  spec.icon = icon;
  if (!placed.empty()) spec.extrasMut().icons.push_back({placed, 0, 0});
  return spec;
}

}

void setUp() {}
void tearDown() {}

static void test_the_setting_switches_enlarging() {
  FakeImages images;
  PageZoom zoom(52, 16, images);
  Settings settings;
  TEST_ASSERT_TRUE(zoom.available());
  TEST_ASSERT_TRUE(zoom.enlarges(settings, page(""), 0));
  settings.enlargeApps = false;
  TEST_ASSERT_FALSE(zoom.enlarges(settings, page(""), 0));
  TEST_ASSERT_EQUAL_INT(26, zoom.stage().width());
  TEST_ASSERT_EQUAL_INT(8, zoom.stage().height());
}

static void test_a_panel_of_eight_rows_is_never_enlarged() {
  FakeImages images;
  PageZoom zoom(32, 8, images);
  TEST_ASSERT_FALSE(zoom.available());
  TEST_ASSERT_FALSE(zoom.enlarges(Settings{}, page(""), 0));
}

static void test_the_stage_follows_the_rows_of_eight() {
  FakeImages images;
  PageZoom zoom(64, 32, images);
  TEST_ASSERT_EQUAL_INT(16, zoom.stage().width());
  TEST_ASSERT_EQUAL_INT(8, zoom.stage().height());
}

static void test_a_page_with_an_image_bigger_than_the_stage_keeps_its_size() {
  FakeImages images;
  PageZoom zoom(52, 16, images);
  const Settings settings;
  TEST_ASSERT_TRUE(zoom.enlarges(settings, page("small"), 0));
  TEST_ASSERT_TRUE(zoom.enlarges(settings, page("unknown"), 0));
  TEST_ASSERT_FALSE(zoom.enlarges(settings, page("big"), 0));
  TEST_ASSERT_FALSE(zoom.enlarges(settings, page("small", "big"), 0));
}

static void test_a_judgment_holds_until_the_images_or_the_assets_change() {
  FakeImages images;
  PageZoom zoom(52, 16, images);
  const Settings settings;
  const AppSpec shown = page("small", "small");
  TEST_ASSERT_TRUE(zoom.enlarges(settings, shown, 0));
  const int probes = images.probes;
  TEST_ASSERT_TRUE(zoom.enlarges(settings, shown, 0));
  TEST_ASSERT_TRUE(zoom.enlarges(settings, page("small"), 0));
  TEST_ASSERT_TRUE(zoom.enlarges(settings, shown, 0));
  TEST_ASSERT_EQUAL_INT(probes + 1, images.probes);
  TEST_ASSERT_FALSE(zoom.enlarges(settings, page("small", "big"), 0));
  TEST_ASSERT_TRUE(zoom.enlarges(settings, shown, 1));
  TEST_ASSERT_TRUE(images.probes > probes + 1);
}

static void test_every_stage_pixel_becomes_a_square_and_the_rest_stays_dark() {
  FakeImages images;
  PageZoom zoom(53, 16, images);
  Canvas& stage = zoom.stage();
  stage.setPixel(0, 0, 0x112233u);
  stage.setPixel(25, 7, 0x445566u);
  Canvas frame(53, 16);
  frame.clear(0xFFFFFFu);
  zoom.present(frame);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x) {
      TEST_ASSERT_EQUAL_HEX32(0x112233u, frame.getPixel(x, y));
      TEST_ASSERT_EQUAL_HEX32(0x445566u, frame.getPixel(50 + x, 14 + y));
    }
  TEST_ASSERT_EQUAL_HEX32(0, frame.getPixel(2, 0));
  for (int y = 0; y < 16; ++y) TEST_ASSERT_EQUAL_HEX32(0, frame.getPixel(52, y));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_setting_switches_enlarging);
  RUN_TEST(test_a_panel_of_eight_rows_is_never_enlarged);
  RUN_TEST(test_the_stage_follows_the_rows_of_eight);
  RUN_TEST(test_a_page_with_an_image_bigger_than_the_stage_keeps_its_size);
  RUN_TEST(test_a_judgment_holds_until_the_images_or_the_assets_change);
  RUN_TEST(test_every_stage_pixel_becomes_a_square_and_the_rest_stays_dark);
  return UNITY_END();
}
