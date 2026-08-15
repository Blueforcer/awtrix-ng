#include <unity.h>


#include "core/payload/PayloadParser.h"
#include "core/render/PaletteStore.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static int dk(DrawKind k) { return static_cast<int>(k); }

static AppSpec parseApp(const char* json, bool isNotif = false) {
  AppSpec s;
  payload::parse(json, isNotif, s);
  return s;
}

static void test_text_string() {
  AppSpec s = parseApp("{\"text\":\"hello\"}");
  TEST_ASSERT_EQUAL_STRING("hello", s.text.c_str());
  TEST_ASSERT_EQUAL_UINT(0u, (unsigned)s.fragments.size());
}

static void test_text_fragments() {
  AppSpec s = parseApp(
      "{\"text\":[{\"text\":\"AB\",\"color\":\"#FF0000\"},{\"text\":\"CD\",\"color\":[0,255,0]}]}");
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)s.fragments.size());
  TEST_ASSERT_EQUAL_STRING("AB", s.fragments[0].text.c_str());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.fragments[0].color);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, s.fragments[1].color);
}

static void test_color_and_defaults() {
  AppSpec s = parseApp("{\"textColor\":\"#123456\"}");
  TEST_ASSERT_TRUE(s.hasTextColor);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, s.textColor);
  TEST_ASSERT_TRUE(s.textCenter);
  TEST_ASSERT_FALSE(s.extras().textUsesPalette);
  TEST_ASSERT_EQUAL_INT(0, s.repeat);
  TEST_ASSERT_EQUAL_INT(-1, s.extrasMut().progress);
}

static void test_durationMs_is_plain_ms() {
  AppSpec s = parseApp("{\"durationMs\":5000}");
  TEST_ASSERT_EQUAL_INT(5000, (int)s.durationMs);
}

static void test_scroll_object_is_parsed() {
  AppSpec s = parseApp("{\"scroll\":{\"mode\":\"bounce\",\"speed\":40}}");
  TEST_ASSERT_TRUE(s.scroll.hasMode);
  TEST_ASSERT_EQUAL_INT((int)ScrollMode::Bounce, (int)s.scroll.mode);
  TEST_ASSERT_EQUAL_INT(40, s.scroll.speed);
  TEST_ASSERT_FALSE(s.scroll.hasGap);
}

static void test_scroll_shorthand_is_parsed() {
  AppSpec s = parseApp("{\"scroll\":\"loop\"}");
  TEST_ASSERT_TRUE(s.scroll.hasMode);
  TEST_ASSERT_EQUAL_INT((int)ScrollMode::Loop, (int)s.scroll.mode);
}

static void test_a_static_scroll_no_longer_overwrites_repeat() {
  AppSpec s = parseApp("{\"scroll\":\"static\",\"repeat\":3}");
  TEST_ASSERT_EQUAL_INT(3, s.repeat);
}

static void test_bar_capped_at_16() {
  AppSpec s = parseApp("{\"barChart\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18]}");
  TEST_ASSERT_EQUAL_UINT(16u, (unsigned)s.extrasMut().barChart.size());
}

static void test_draw_array_form() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"draw\":[[\"pixel\",1,2,\"#FF0000\"],"
      "[\"line\",0,7,31,7,\"#00FF00\"],"
      "[\"rectFill\",2,2,4,4,\"#0000FF\"],"
      "[\"circleFill\",16,4,3,\"#0F0\"],"
      "[\"text\",9,1,\"HI\",\"#FFFFFF\"]]}",
      false, s, nullptr, nullptr, &err));
  const std::vector<DrawOp>& d = s.extras().draw;
  TEST_ASSERT_EQUAL_UINT(5u, (unsigned)d.size());
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::Pixel), dk(d[0].kind));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, d[0].color);
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::Line), dk(d[1].kind));
  TEST_ASSERT_EQUAL_INT(31, d[1].x2);
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::FillRect), dk(d[2].kind));
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::FillCircle), dk(d[3].kind));
  TEST_ASSERT_EQUAL_INT(3, d[3].r);
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::Text), dk(d[4].kind));
  TEST_ASSERT_EQUAL_STRING("HI", d[4].text.c_str());
}

static void test_draw_order_follows_the_array() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"draw\":[[\"pixel\",0,0,\"#111111\"],[\"pixel\",1,0,\"#222222\"],"
      "[\"pixel\",2,0,\"#333333\"]]}", false, s));
  const std::vector<DrawOp>& d = s.extras().draw;
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)d.size());
  TEST_ASSERT_EQUAL_HEX32(0x111111u, d[0].color);
  TEST_ASSERT_EQUAL_HEX32(0x333333u, d[2].color);
}

static void test_draw_color_may_be_omitted() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"draw\":[[\"pixel\",1,2]]}", false, s));
  TEST_ASSERT_TRUE(s.extras().draw[0].inheritColor);
}

static void test_draw_pixels_command() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"draw\":[[\"pixels\",\"#0F0\",0,0,1,1,2,2]]}", false, s));
  const DrawOp& op = s.extras().draw[0];
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::Pixels), dk(op.kind));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, op.color);
  TEST_ASSERT_EQUAL_UINT(6u, (unsigned)op.points.size());
  TEST_ASSERT_EQUAL_INT(2, op.points[4]);
}

static void test_draw_pixels_inherits_color_on_null() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"draw\":[[\"pixels\",null,0,0,1,1]]}", false, s));
  TEST_ASSERT_TRUE(s.extras().draw[0].inheritColor);
}

static void test_draw_unknown_command_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"draw\":[[\"pixel\",0,0],[\"squircle\",1,1]]}",
                                   false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("draw[1]", err.field.c_str());
}

static void test_draw_wrong_arity_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"draw\":[[\"line\",0,0,5]]}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("draw[0]", err.field.c_str());
}

static void test_draw_pixels_odd_tail_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"draw\":[[\"pixels\",\"#0F0\",0,0,1]]}",
                                   false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("draw[0]", err.field.c_str());
}

static void test_draw_non_array_element_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"draw\":[{\"pixel\":[0,0]}]}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("draw[0]", err.field.c_str());
}

static void test_draw_non_numeric_coordinate_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"draw\":[[\"pixel\",\"x\",2]]}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("draw[0]", err.field.c_str());
}

static void test_draw_bitmap_base64() {
  AppSpec s = parseApp("{\"draw\":[[\"bitmap\",3,1,2,1,\"AAD/AP8A\"]]}");
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)s.extrasMut().draw.size());
  const DrawOp& op = s.extrasMut().draw[0];
  TEST_ASSERT_EQUAL_INT(dk(DrawKind::Bitmap), dk(op.kind));
  TEST_ASSERT_EQUAL_INT(3, op.x);
  TEST_ASSERT_EQUAL_INT(2, op.w);
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)op.bitmap.size());
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, op.bitmap[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, op.bitmap[1]);
}

static void test_bitmap_array_takes_normal_colors() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"draw\":[[\"bitmap\",0,0,2,1,[\"#FF0000\",[0,255,0]]]]}", false, s));
  const DrawOp& op = s.extras().draw[0];
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)op.bitmap.size());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, op.bitmap[0]);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, op.bitmap[1]);
}

static void test_bitmap_array_rejects_a_bad_color() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse(
      "{\"draw\":[[\"bitmap\",0,0,2,1,[\"#FF0000\",\"nope\"]]]}",
      false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("draw[0]", err.field.c_str());
}

static void test_bad_color_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"textColor\":\"#GGGGGG\"}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("textColor", err.field.c_str());
}

static void test_good_color_still_parses() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_TRUE(payload::parse("{\"textColor\":\"#0F0\"}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_TRUE(s.hasTextColor);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, s.textColor);
}

static void test_notification_fields() {
  AppSpec s = parseApp("{\"text\":\"x\",\"hold\":true,\"sound\":5}", true);
  TEST_ASSERT_TRUE(s.isNotification);
  TEST_ASSERT_TRUE(s.hold);
  TEST_ASSERT_TRUE(s.stack);
  TEST_ASSERT_EQUAL_STRING("5", s.sound.c_str());
}

static void test_text_case_words() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"textCase\":\"upper\"}", false, s));
  TEST_ASSERT_EQUAL_INT((int)TextCase::Upper, (int)s.textCase);
  AppSpec t;
  TEST_ASSERT_TRUE(payload::parse("{\"textCase\":\"asTyped\"}", false, t));
  TEST_ASSERT_EQUAL_INT((int)TextCase::AsTyped, (int)t.textCase);
  AppSpec u;
  TEST_ASSERT_TRUE(payload::parse("{}", false, u));
  TEST_ASSERT_EQUAL_INT((int)TextCase::Inherit, (int)u.textCase);
}

static void test_icon_mode_words() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"iconMode\":\"pushOnce\"}", false, s));
  TEST_ASSERT_EQUAL_INT((int)IconMode::PushOnce, (int)s.iconMode);
  AppSpec t;
  TEST_ASSERT_TRUE(payload::parse("{\"iconMode\":\"push\"}", false, t));
  TEST_ASSERT_EQUAL_INT((int)IconMode::Push, (int)t.iconMode);
}

static void test_lifetime_expiry_words() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"lifetimeExpiry\":\"mark\"}", false, s));
  TEST_ASSERT_EQUAL_INT((int)LifetimeExpiry::Mark, (int)s.lifetimeExpiry);
}

static void test_unknown_mode_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"iconMode\":\"slide\"}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("iconMode", err.field.c_str());
}

static void test_numeric_mode_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"textCase\":1}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("textCase", err.field.c_str());
}

static void test_renamed_keys_parse() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"textColor\":\"#FF0000\",\"backgroundColor\":\"#101010\",\"textCenter\":false,"
      "\"palette\":\"Heat\",\"textInFront\":true,\"textOffsetX\":3,\"iconOffsetX\":2,"
      "\"barChart\":[1,2,3],\"lineChart\":[4,5],\"chartAutoscale\":false,"
      "\"chartColor\":\"#00FF00\",\"progressColor\":\"#0000FF\","
      "\"progressTrackColor\":\"#202020\"}",
      false, s));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.textColor);
  TEST_ASSERT_EQUAL_HEX32(0x101010u, s.backgroundColor);
  TEST_ASSERT_FALSE(s.textCenter);
  TEST_ASSERT_TRUE(s.extras().palette.valid());
  TEST_ASSERT_TRUE(s.textInFront);
  TEST_ASSERT_EQUAL_INT(3, s.textOffsetX);
  TEST_ASSERT_EQUAL_INT(2, s.iconOffsetX);
  TEST_ASSERT_EQUAL_UINT(3u, (unsigned)s.extras().barChart.size());
  TEST_ASSERT_EQUAL_UINT(2u, (unsigned)s.extras().lineChart.size());
  TEST_ASSERT_FALSE(s.extras().chartAutoscale);
  TEST_ASSERT_TRUE(s.extras().hasChartColor);
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, s.extras().chartColor);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, s.extras().progressColor);
  TEST_ASSERT_EQUAL_HEX32(0x202020u, s.extras().progressTrackColor);
}

static void test_fragment_keys_are_spelled_out() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"text\":[{\"text\":\"AB\",\"color\":\"#FF0000\"}]}", false, s));
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)s.fragments.size());
  TEST_ASSERT_EQUAL_STRING("AB", s.fragments[0].text.c_str());
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.fragments[0].color);
}

static void test_unknown_key_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"progressC\":\"#FF0000\"}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("progressC", err.field.c_str());
}

static void test_notification_key_on_an_app_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"wakeup\":true}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("wakeup", err.field.c_str());
}

static void test_notification_keys_pass_on_a_notification() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"wakeup\":true,\"hold\":true,\"soundLoop\":true,"
      "\"soundRtttl\":\"a:d=4,o=5,b=120:c\"}",
      true, s));
  TEST_ASSERT_TRUE(s.wakeup);
  TEST_ASSERT_TRUE(s.hold);
  TEST_ASSERT_TRUE(s.loopSound);
  TEST_ASSERT_EQUAL_STRING("a:d=4,o=5,b=120:c", s.extras().rtttl.c_str());
}

// A melody that cannot be played is a mistake in the request, not something to discover as
// silence once the notification is already on screen.
static void test_an_unparsable_sound_rtttl_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(
      payload::parse("{\"soundRtttl\":\"a:d=4;\"}", true, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("soundRtttl", err.field.c_str());
  TEST_ASSERT_FALSE(err.message.empty());
}

// Clients that serialise their whole schema send every field they know about, empty ones
// included; an empty melody is the absence of one, not a malformed one.
static void test_an_empty_sound_rtttl_is_accepted() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_TRUE(payload::parse("{\"text\":\"A\",\"soundRtttl\":\"\",\"sound\":\"\"}", true, s,
                                  nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("A", s.text.c_str());
  TEST_ASSERT_TRUE(s.extras().rtttl.empty());
  TEST_ASSERT_TRUE(s.sound.empty());
}

static void test_effect_overlay_names() {
  AppSpec s = parseApp("{\"effect\":\"Matrix\",\"overlay\":\"snow\"}");
  TEST_ASSERT_EQUAL_STRING("Matrix", s.effect.c_str());
  TEST_ASSERT_EQUAL_STRING("snow", s.overlay.c_str());
}

static void test_bad_json_returns_false() {
  AppSpec s;
  TEST_ASSERT_FALSE(payload::parse("{not valid", false, s));
}

static void test_array_payload_parses_first() {
  AppSpec s = parseApp("[{\"text\":\"first\"},{\"text\":\"second\"}]");
  TEST_ASSERT_EQUAL_STRING("first", s.text.c_str());
}

static void test_utf8_reaches_the_spec_intact() {
  std::string j = "{\"text\":\"";
  j += static_cast<char>(0xC3); j += static_cast<char>(0xA4);
  j += static_cast<char>(0xC2); j += static_cast<char>(0xB0);
  j += "\"}";
  AppSpec s;
  payload::parse(j, false, s);
  TEST_ASSERT_EQUAL_UINT(4u, (unsigned)s.text.size());
  TEST_ASSERT_EQUAL_HEX8(0xC3, (uint8_t)s.text[0]);
  TEST_ASSERT_EQUAL_HEX8(0xA4, (uint8_t)s.text[1]);
  TEST_ASSERT_EQUAL_HEX8(0xC2, (uint8_t)s.text[2]);
  TEST_ASSERT_EQUAL_HEX8(0xB0, (uint8_t)s.text[3]);
}

static void test_utf8_escapes_decode_to_the_same_bytes() {
  AppSpec s;
  payload::parse("{\"text\":\"\\u00e4\\u00b0\"}", false, s);
  TEST_ASSERT_EQUAL_UINT(4u, (unsigned)s.text.size());
  TEST_ASSERT_EQUAL_HEX8(0xC3, (uint8_t)s.text[0]);
  TEST_ASSERT_EQUAL_HEX8(0xA4, (uint8_t)s.text[1]);
}

static void test_font_defaults_to_small() {
  TEST_ASSERT_EQUAL_INT(0, (int)parseApp("{\"text\":\"hi\"}").font);
}

static void test_font_is_selectable() {
  TEST_ASSERT_EQUAL_INT((int)FontId::Large, (int)parseApp("{\"font\":\"large\"}").font);
  TEST_ASSERT_EQUAL_INT((int)FontId::Small, (int)parseApp("{\"font\":\"small\"}").font);
}

static void test_an_unknown_font_name_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"font\":\"huge\"}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("font", err.field.c_str());
}

static void test_overlay_lowercased() {
  AppSpec s = parseApp("{\"overlay\":\"SNOW\"}");
  TEST_ASSERT_EQUAL_STRING("snow", s.overlay.c_str());
}

static void test_palette_by_stock_name() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"effect\":\"Plasma\",\"palette\":\"Ocean\"}", false, s));
  TEST_ASSERT_TRUE(s.extras().palette.valid());
  TEST_ASSERT_EQUAL_HEX32(render::namedPalette("Ocean").entries[0],
                          s.extras().palette.palette().entries[0]);
  TEST_ASSERT_TRUE(s.extras().palette.blend);
}

static void test_palette_stops_are_spread_over_all_entries() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":[\"FF0000\",\"0000FF\"]}", false, s));
  const render::Palette& p = s.extras().palette.palette();
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
  TEST_ASSERT_TRUE(p.entries[8] != 0xFF0000u && p.entries[8] != 0x0000FFu);
}

static void test_positioned_stops_are_read() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"palette\":[{\"color\":\"#FF0000\",\"pos\":75},{\"color\":[0,0,255],\"pos\":100}]}", false,
      s));
  const render::Palette& p = s.extras().palette.palette();
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, p.entries[11]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, p.entries[15]);
}

static void test_end_positions_match_the_plain_list() {
  AppSpec a, b;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":[\"#FF0000\",\"#0000FF\"]}", false, a));
  TEST_ASSERT_TRUE(payload::parse(
      "{\"palette\":[{\"color\":\"#FF0000\",\"pos\":0},{\"color\":\"#0000FF\",\"pos\":100}]}", false,
      b));
  for (int i = 0; i < 16; ++i)
    TEST_ASSERT_EQUAL_HEX32(a.extras().palette.palette().entries[i],
                            b.extras().palette.palette().entries[i]);
}

static void test_stops_out_of_order_are_sorted() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"palette\":[{\"color\":\"#0000FF\",\"pos\":100},{\"color\":\"#FF0000\",\"pos\":0}]}", false,
      s));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.extras().palette.palette().entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, s.extras().palette.palette().entries[15]);
}

static void test_bad_positioned_stops_are_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse(
      "{\"palette\":[{\"color\":\"#FF0000\",\"pos\":0},\"#0000FF\"]}", false, s, nullptr, nullptr,
      &err));
  TEST_ASSERT_EQUAL_STRING("palette", err.field.c_str());
  TEST_ASSERT_FALSE(
      payload::parse("{\"palette\":[{\"color\":\"#FF0000\",\"pos\":101}]}", false, s));
  TEST_ASSERT_FALSE(payload::parse("{\"palette\":[{\"pos\":50}]}", false, s));
  TEST_ASSERT_FALSE(payload::parse("{\"palette\":[{\"color\":\"#FF0000\"}]}", false, s));
}

static void test_unknown_palette_is_rejected() {
  AppSpec s;
  DispatchDetail err;
  TEST_ASSERT_FALSE(payload::parse("{\"palette\":\"Nope\"}", false, s, nullptr, nullptr, &err));
  TEST_ASSERT_EQUAL_STRING("palette", err.field.c_str());
}

static void test_palette_loader_resolves_user_name() {
  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    if (name != "MyPal") return false;
    for (int i = 0; i < 16; ++i) out.entries[i] = 0x123456u;
    return true;
  });
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":\"MyPal\"}", false, s));
  TEST_ASSERT_EQUAL_HEX32(0x123456u, s.extras().palette.palette().entries[0]);
  TEST_ASSERT_EQUAL_HEX32(0x123456u, s.extras().palette.palette().entries[15]);
  render::setPaletteLoader(nullptr);
  render::clearPaletteCache();
}

static void test_a_file_shadows_the_builtin_of_the_same_name() {
  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    if (name != "Ocean") return false;
    for (int i = 0; i < 16; ++i) out.entries[i] = 0xBADBADu;
    return true;
  });
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":\"Ocean\"}", false, s));
  TEST_ASSERT_EQUAL_HEX32(0xBADBADu, s.extras().palette.palette().entries[0]);
  render::setPaletteLoader(nullptr);
  render::clearPaletteCache();

  AppSpec plain;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":\"Ocean\"}", false, plain));
  TEST_ASSERT_EQUAL_HEX32(0x191970u, plain.extras().palette.palette().entries[0]);
  render::clearPaletteCache();
}

static void test_same_palette_name_is_shared() {
  AppSpec a, b;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":\"Lava\"}", false, a));
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":\"Lava\"}", false, b));
  TEST_ASSERT_EQUAL_PTR(a.extras().palette.pal.get(), b.extras().palette.pal.get());
}

static void test_palette_absent_by_default() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"effect\":\"Plasma\"}", false, s));
  TEST_ASSERT_FALSE(s.extras().palette.valid());
  TEST_ASSERT_FALSE(s.extras().hasEffectSpeed);
}

static void test_paint_sentinel_selects_the_palette() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse(
      "{\"palette\":\"Heat\",\"textColor\":\"palette\",\"chartColor\":\"palette\","
      "\"progressColor\":\"palette\"}",
      false, s));
  TEST_ASSERT_TRUE(s.extras().textUsesPalette);
  TEST_ASSERT_TRUE(s.extras().chartUsesPalette);
  TEST_ASSERT_TRUE(s.extras().progressUsesPalette);
  TEST_ASSERT_FALSE(s.hasTextColor);
  TEST_ASSERT_FALSE(s.extras().hasChartColor);
}

static void test_paint_field_still_takes_a_colour() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"textColor\":\"FF0000\"}", false, s));
  TEST_ASSERT_TRUE(s.hasTextColor);
  TEST_ASSERT_FALSE(s.extras().textUsesPalette);
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, s.textColor);
}

static void test_palette_span_and_speed() {
  AppSpec s;
  TEST_ASSERT_TRUE(
      payload::parse("{\"palette\":\"Heat\",\"paletteSpan\":24,\"paletteSpeed\":2}", false, s));
  TEST_ASSERT_EQUAL_UINT16(24, s.extras().palette.spanPx);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, s.extras().palette.speed);
}

static void test_palette_blend_can_be_turned_off() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"palette\":\"Heat\",\"paletteBlend\":false}", false, s));
  TEST_ASSERT_FALSE(s.extras().palette.blend);
}

static void test_effect_speed_is_clamped() {
  AppSpec s;
  TEST_ASSERT_TRUE(payload::parse("{\"effectSpeed\":0}", false, s));
  TEST_ASSERT_EQUAL_FLOAT(0.1f, s.extras().effectSpeed);
  TEST_ASSERT_TRUE(payload::parse("{\"effectSpeed\":-5}", false, s));
  TEST_ASSERT_EQUAL_FLOAT(0.1f, s.extras().effectSpeed);
  TEST_ASSERT_TRUE(payload::parse("{\"effectSpeed\":100}", false, s));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, s.extras().effectSpeed);
}


static void test_valid_but_oversized_payload_is_not_reported_as_malformed() {
  std::string wide = "{";
  for (int i = 0; i < 600; ++i) {
    if (i) wide += ',';
    wide += "\"key" + std::to_string(i) + "\":\"value" + std::to_string(i) + "\"";
  }
  wide += "}";

  AppSpec s;
  DispatchDetail err;
  payload::JsonParse why = payload::JsonParse::Ok;
  TEST_ASSERT_FALSE(payload::parse(wide, false, s, nullptr, &why, &err));
  TEST_ASSERT_EQUAL_INT((int)payload::JsonParse::Ok, (int)why);
  TEST_ASSERT_EQUAL_STRING("key0", err.field.c_str());

  std::string wideValid = "{\"text\":\"x\"";
  for (int i = 0; i < 600; ++i) wideValid += ",\"repeat\":2";
  wideValid += "}";
  AppSpec ok;
  why = payload::JsonParse::Malformed;
  TEST_ASSERT_TRUE(payload::parse(wideValid, false, ok, nullptr, &why));
  TEST_ASSERT_EQUAL_INT((int)payload::JsonParse::Ok, (int)why);
  TEST_ASSERT_EQUAL_INT(2, ok.repeat);

  why = payload::JsonParse::Ok;
  TEST_ASSERT_FALSE(payload::parse("{not valid", false, s, nullptr, &why));
  TEST_ASSERT_EQUAL_INT((int)payload::JsonParse::Malformed, (int)why);
  TEST_ASSERT_EQUAL_INT((int)DispatchResult::ParseError, (int)payload::toDispatchResult(why));

  why = payload::JsonParse::Malformed;
  TEST_ASSERT_TRUE(payload::parse("{\"text\":\"hi\"}", false, s, nullptr, &why));
  TEST_ASSERT_EQUAL_INT((int)payload::JsonParse::Ok, (int)why);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_but_oversized_payload_is_not_reported_as_malformed);

  RUN_TEST(test_palette_by_stock_name);
  RUN_TEST(test_palette_stops_are_spread_over_all_entries);
  RUN_TEST(test_positioned_stops_are_read);
  RUN_TEST(test_end_positions_match_the_plain_list);
  RUN_TEST(test_stops_out_of_order_are_sorted);
  RUN_TEST(test_bad_positioned_stops_are_rejected);
  RUN_TEST(test_unknown_palette_is_rejected);
  RUN_TEST(test_palette_loader_resolves_user_name);
  RUN_TEST(test_a_file_shadows_the_builtin_of_the_same_name);
  RUN_TEST(test_same_palette_name_is_shared);
  RUN_TEST(test_palette_absent_by_default);
  RUN_TEST(test_paint_sentinel_selects_the_palette);
  RUN_TEST(test_paint_field_still_takes_a_colour);
  RUN_TEST(test_palette_span_and_speed);
  RUN_TEST(test_palette_blend_can_be_turned_off);
  RUN_TEST(test_effect_speed_is_clamped);
  RUN_TEST(test_text_string);
  RUN_TEST(test_utf8_reaches_the_spec_intact);
  RUN_TEST(test_utf8_escapes_decode_to_the_same_bytes);
  RUN_TEST(test_font_defaults_to_small);
  RUN_TEST(test_font_is_selectable);
  RUN_TEST(test_an_unknown_font_name_is_rejected);
  RUN_TEST(test_overlay_lowercased);
  RUN_TEST(test_text_fragments);
  RUN_TEST(test_color_and_defaults);
  RUN_TEST(test_durationMs_is_plain_ms);
  RUN_TEST(test_scroll_object_is_parsed);
  RUN_TEST(test_scroll_shorthand_is_parsed);
  RUN_TEST(test_a_static_scroll_no_longer_overwrites_repeat);
  RUN_TEST(test_bar_capped_at_16);
  RUN_TEST(test_draw_array_form);
  RUN_TEST(test_draw_order_follows_the_array);
  RUN_TEST(test_draw_color_may_be_omitted);
  RUN_TEST(test_draw_pixels_command);
  RUN_TEST(test_draw_pixels_inherits_color_on_null);
  RUN_TEST(test_draw_unknown_command_is_rejected);
  RUN_TEST(test_draw_wrong_arity_is_rejected);
  RUN_TEST(test_draw_pixels_odd_tail_is_rejected);
  RUN_TEST(test_draw_non_array_element_is_rejected);
  RUN_TEST(test_draw_non_numeric_coordinate_is_rejected);
  RUN_TEST(test_draw_bitmap_base64);
  RUN_TEST(test_bitmap_array_takes_normal_colors);
  RUN_TEST(test_bitmap_array_rejects_a_bad_color);
  RUN_TEST(test_bad_color_is_rejected);
  RUN_TEST(test_good_color_still_parses);
  RUN_TEST(test_notification_fields);
  RUN_TEST(test_text_case_words);
  RUN_TEST(test_icon_mode_words);
  RUN_TEST(test_lifetime_expiry_words);
  RUN_TEST(test_unknown_mode_is_rejected);
  RUN_TEST(test_numeric_mode_is_rejected);
  RUN_TEST(test_renamed_keys_parse);
  RUN_TEST(test_fragment_keys_are_spelled_out);
  RUN_TEST(test_unknown_key_is_rejected);
  RUN_TEST(test_notification_key_on_an_app_is_rejected);
  RUN_TEST(test_notification_keys_pass_on_a_notification);
  RUN_TEST(test_an_unparsable_sound_rtttl_is_rejected);
  RUN_TEST(test_an_empty_sound_rtttl_is_accepted);
  RUN_TEST(test_effect_overlay_names);
  RUN_TEST(test_bad_json_returns_false);
  RUN_TEST(test_array_payload_parses_first);
  return UNITY_END();
}
