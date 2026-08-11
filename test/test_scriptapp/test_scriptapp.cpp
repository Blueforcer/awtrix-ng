#include <unity.h>

#include <string>

#include "core/apps/IApp.h"
#include "core/render/Canvas.h"
#include "core/render/TextRenderer.h"
#include "core/script/BerryVM.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptBindings.h"
#include "core/script/ScriptHeapTesting.h"
#include "core/script/ScriptServices.h"
#include "core/script/SharedState.h"

using namespace awtrix;

static const FontGlyph kG = {0, 3, 3, 4, 0, 0};
static const FontGlyph kGlyphs[] = {kG, kG, kG, kG, kG, kG, kG, kG, kG, kG, kG, kG, kG,
                                    kG, kG, kG, kG, kG, kG, kG, kG, kG, kG, kG, kG, kG};
static const uint8_t kBitmap[] = {0xFF, 0x80};
static const GfxFont kFont = {kBitmap, kGlyphs, 'A', 'Z', 8};

static script::ScriptServices g_svc;

void setUp() {
  g_svc.http = nullptr;
  g_svc.mqtt = nullptr;
  g_svc.icon = nullptr;
  g_svc.storeSink = nullptr;
  g_svc.shared = nullptr;
  script::setServices(&g_svc);
}

void tearDown() { script::setServices(nullptr); }

static bool loadWithPrelude(script::BerryVM& vm, const char* user) {
  std::string err;
  if (!script::installBindings(vm, err)) {
    TEST_MESSAGE(err.c_str());
    return false;
  }
  if (!vm.load(user)) {
    TEST_MESSAGE(vm.lastError().c_str());
    return false;
  }
  return true;
}


static void test_draw_primitives() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  pixel(1, 2, 0xFF0000)\n"
                                   "  rect_fill(4, 0, 2, 2, 0x00FF00)\n"
                                   "  pixel(width() - 1, height() - 1, 0x0000FF)\n"
                                   "end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope scope(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(1, 2));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(5, 1));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(31, 7));
}

static void test_shape_primitives() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  clear(0x010101)\n"
                                   "  line(0, 0, 3, 0, 0xFF0000)\n"
                                   "  rect(10, 1, 4, 4, 0x00FF00)\n"
                                   "  circle_fill(24, 4, 2, 0x0000FF)\n"
                                   "end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope scope(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x010101u, c.getPixel(31, 7));
  TEST_ASSERT_EQUAL_HEX32(0xFF0000u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(0x00FF00u, c.getPixel(10, 1));
  TEST_ASSERT_EQUAL_HEX32(0x010101u, c.getPixel(11, 2));
  TEST_ASSERT_EQUAL_HEX32(0x0000FFu, c.getPixel(24, 4));
}

static void test_draw_noop_without_canvas() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "def loop() pixel(0,0,0xFF) end"));
  script::BindingScope scope(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("loop"));
}

static void test_width_height_zero_without_canvas() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "var w = -1\ndef loop() w = width() + height() end"));
  Canvas c(32, 8);
  {
    script::BindingScope s(nullptr, nullptr, "T");
    TEST_ASSERT_TRUE(vm.call("loop"));
  }
  TEST_ASSERT_TRUE(vm.load("def draw() pixel(0, 0, w == 0 ? 0x22 : 0x99) end"));
  {
    script::BindingScope s(&c, nullptr, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(0, 0));
}


static void test_text_draws_with_real_font() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "var adv = 0\ndef draw() adv = text(0, 0, 'HI', 0xFFFFFF) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  ctx.font = &kFont;
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  int lit = 0;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      if (c.getPixel(x, y) != 0) ++lit;
  TEST_ASSERT_TRUE(lit > 0);
  TEST_ASSERT_EQUAL_INT(18, lit);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(3, 0));
}

static void test_text_width_matches_renderer() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "def draw() pixel(0, 0, text_width('HI')) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  ctx.font = &kFont;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_INT(8, text::width(kFont, "HI"));
  TEST_ASSERT_EQUAL_HEX32(8u, c.getPixel(0, 0));
}

static void test_ink_width_reports_the_lit_pixels_not_the_advance() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm, "def draw() pixel(0, 0, text_ink_width('HI')) pixel(1, 0, text_width('HI')) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  ctx.font = &kFont;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  const text::TextMetrics m = text::measure(kFont, "HI");
  TEST_ASSERT_EQUAL_HEX32(static_cast<uint32_t>(m.inkWidth()), c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(static_cast<uint32_t>(m.advance), c.getPixel(1, 0));
  TEST_ASSERT_TRUE(m.inkWidth() < m.advance);
}

static void test_ink_width_reports_the_sentinel_without_a_font() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "var w = 0\n"
                                       "def draw() w = text_ink_width('HI') end\n"
                                       "def check() return str(w) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("-1", out.c_str());
}

static void test_text_noop_without_font() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "def draw() pixel(0, 0, text(1, 1, 'HI', 0xFFFFFF) + 5) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(5u, c.getPixel(0, 0));
}


static void test_time_builtins_read_render_ctx() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  pixel(0, 0, hour())\n"
                                   "  pixel(1, 0, minute())\n"
                                   "  pixel(2, 0, second())\n"
                                   "  pixel(3, 0, weekday())\n"
                                   "  pixel(4, 0, day())\n"
                                   "  pixel(5, 0, month())\n"
                                   "  pixel(6, 0, year() - 2000)\n"
                                   "end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  ctx.hour = 13;
  ctx.minute = 45;
  ctx.second = 9;
  ctx.weekday = 3;
  ctx.mday = 27;
  ctx.month = 11;
  ctx.year = 2031;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(13u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(45u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(9u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(3u, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(27u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(11u, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(31u, c.getPixel(6, 0));
}

static void test_epoch_ms_reports_the_wall_clock() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "var s = ''\n"
                                   "def draw()\n"
                                   "  s = str(epoch_ms()) + ',' + str(epoch_ms() % 1000)\n"
                                   "end\n"
                                   "def check() return s end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  ctx.epochMs = 1753876543210LL;
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("1753876543210,210", out.c_str());

  RenderCtx unsynced;
  {
    script::BindingScope s(&c, &unsynced, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("-1,-1", out.c_str());
}

static void test_time_builtins_report_a_sentinel_without_a_clock() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var s = ''\n"
      "def setup()\n"
      "  s = str(hour())+','+str(minute())+','+str(second())+','+str(epoch_ms())\n"
      "  s += ','+str(weekday())+','+str(day())+','+str(month())+','+str(year())\n"
      "  s += ','+str(text_width('HI'))\n"
      "end\n"
      "def check() return s end"));
  {
    script::BindingScope sc(nullptr, nullptr, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("-1,-1,-1,-1,-1,-1,-1,-1,-1", out.c_str());
}

static void test_text_width_reports_the_sentinel_without_a_font() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "var w = 0\ndef draw() w = text_width('HI') end\n"
                                       "def check() return str(w) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("-1", out.c_str());
}

static void test_now_ms_uses_injected_clock() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "var a = 0\ndef setup() a = now_ms() end"));
  {
    script::BindingScope s(nullptr, nullptr, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_TRUE(vm.load("def draw() pixel(0, 0, a > 0 ? 0x33 : 0) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x33u, c.getPixel(0, 0));
}


static void test_http_module_soft_fails_without_transport() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var got = 'unset'\n"
      "def setup() http.get('http://x/', def(b) got = b == nil ? 'nil' : 'body' end) end\n"
      "def draw() if got == 'nil' pixel(0,0,0xAA) end end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_HEX32(0xAAu, c.getPixel(0, 0));
}

namespace {
struct FakeHttp : script::IScriptHttp {
  uint32_t lastId = 0;
  std::string lastUrl;
  std::size_t lastMax = 0;
  std::string lastMethod;
  std::string lastBody;
  script::HttpHeaders lastHeaders;
  int calls = 0;
  bool accept = true;
  bool request(const script::HttpRequest& req) override {
    lastId = req.id;
    lastUrl = req.url;
    lastMax = req.maxBytes;
    lastMethod = req.method;
    lastBody = req.body;
    lastHeaders = req.headers;
    ++calls;
    return accept;
  }
  std::string header(const std::string& name) const {
    for (const auto& h : lastHeaders)
      if (h.first == name) return h.second;
    return "";
  }
};
}

static void test_http_dispatch_roundtrip() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var temp = -1\n"
      "def setup() http.get('http://x/', def(b) temp = int(json.load(b)['t']) end) end\n"
      "def draw() if temp >= 0 pixel(0, 0, temp) end end"));
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_EQUAL_UINT(1u, (unsigned)fake.lastId);
  TEST_ASSERT_EQUAL_STRING("http://x/", fake.lastUrl.c_str());
  TEST_ASSERT_EQUAL_UINT((unsigned)script::kMaxHttpBody, (unsigned)fake.lastMax);

  TEST_ASSERT_TRUE(vm.call3("_dispatch_http_str", "1", "200", "{\"t\": 21}"));
  Canvas c(32, 8);
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_HEX32(21u, c.getPixel(0, 0));
}

static void test_http_opts_cap_raises_the_response_limit() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm, "def setup() http.get('http://x/', def(b) end, {'cap': 20000}) end"));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
  TEST_ASSERT_EQUAL_UINT(20000u, (unsigned)fake.lastMax);
}

static void test_http_opts_cap_ignores_a_non_positive_value() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm, "def setup() http.get('http://x/', def(b) end, {'cap': -5}) end"));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
  TEST_ASSERT_EQUAL_UINT((unsigned)script::kMaxHttpBody, (unsigned)fake.lastMax);
}

static void test_http_failure_dispatch_delivers_nil() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var got = 'unset'\n"
      "def setup() http.get('http://x/', def(b) got = b == nil ? 'nil' : 'body' end) end\n"
      "def draw() if got == 'nil' pixel(0,0,0xBB) end end"));
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_TRUE(vm.call2("_dispatch_http_fail", "1", "0"));
  Canvas c(32, 8);
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_HEX32(0xBBu, c.getPixel(0, 0));
}

static void test_http_callback_fires_once() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var n = 0\n"
      "def setup() http.get('http://x/', def(b) n += 1 end) end\n"
      "def draw() pixel(0, 0, n) end"));
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_TRUE(vm.call3("_dispatch_http_str", "1", "200", "x"));
  TEST_ASSERT_TRUE(vm.call3("_dispatch_http_str", "1", "200", "x"));
  TEST_ASSERT_TRUE(vm.call3("_dispatch_http_str", "99", "200", "x"));
  Canvas c(32, 8);
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(1u, c.getPixel(0, 0));
}

static void test_http_get_defaults_to_get_without_body_or_headers() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(
      loadWithPrelude(vm, "def setup() http.get('http://x/', def(b, st) end) end"));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
  TEST_ASSERT_EQUAL_STRING("GET", fake.lastMethod.c_str());
  TEST_ASSERT_EQUAL_STRING("", fake.lastBody.c_str());
  TEST_ASSERT_TRUE(fake.lastHeaders.empty());
}

static void test_http_post_carries_method_body_and_headers() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "def setup()\n"
      "  http.post('http://x/api', '{\"a\":1}', def(b, st) end,\n"
      "            {'headers': {'Authorization': 'Bearer TOK'}})\n"
      "end"));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
  TEST_ASSERT_EQUAL_STRING("POST", fake.lastMethod.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", fake.lastBody.c_str());
  TEST_ASSERT_EQUAL_STRING("Bearer TOK", fake.header("Authorization").c_str());
}

static void test_http_verbs_reach_the_transport() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "def put() http.put('http://x/', 'p', def(b, st) end) end\n"
      "def patch() http.patch('http://x/', 'q', def(b, st) end) end\n"
      "def del() http.delete('http://x/', def(b, st) end) end\n"
      "def generic() http.request('DELETE', 'http://x/', def(b, st) end, {'body': 'z'}) end"));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");

  TEST_ASSERT_TRUE(vm.call("put"));
  TEST_ASSERT_EQUAL_STRING("PUT", fake.lastMethod.c_str());
  TEST_ASSERT_EQUAL_STRING("p", fake.lastBody.c_str());

  TEST_ASSERT_TRUE(vm.call("patch"));
  TEST_ASSERT_EQUAL_STRING("PATCH", fake.lastMethod.c_str());
  TEST_ASSERT_EQUAL_STRING("q", fake.lastBody.c_str());

  TEST_ASSERT_TRUE(vm.call("del"));
  TEST_ASSERT_EQUAL_STRING("DELETE", fake.lastMethod.c_str());
  TEST_ASSERT_EQUAL_STRING("", fake.lastBody.c_str());

  TEST_ASSERT_TRUE(vm.call("generic"));
  TEST_ASSERT_EQUAL_STRING("DELETE", fake.lastMethod.c_str());
  TEST_ASSERT_EQUAL_STRING("z", fake.lastBody.c_str());
}

static void test_http_rejects_bad_method_and_malformed_header_before_the_transport() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  const std::string src =
      "var fails = 0\n"
      "def bad_method() http.request('TRACE', 'http://x/', def(b, st) fails += 1 end) end\n"
      "def bad_header()\n"
      "  http.get('http://x/', def(b, st) fails += 1 end, {'headers': {'X': \"a\\nB: c\"}})\n"
      "end\n"
      "def check() return str(fails) end";
  TEST_ASSERT_TRUE(loadWithPrelude(vm, src.c_str()));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("bad_method"));
  TEST_ASSERT_TRUE(vm.call("bad_header"));

  TEST_ASSERT_EQUAL_INT(0, fake.calls);
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("2", out.c_str());
}

static void test_http_callback_receives_the_status() {
  FakeHttp fake;
  g_svc.http = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var seen = ''\n"
      "def setup()\n"
      "  http.get('http://x/', def(b, st) seen = str(st) + ':' + (b == nil ? 'nil' : b) end)\n"
      "end\n"
      "def check() return seen end"));
  RenderCtx ctx;
  script::BindingScope s(nullptr, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));

  TEST_ASSERT_TRUE(vm.call3("_dispatch_http_str", "1", "401", "denied"));
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("401:denied", out.c_str());

  TEST_ASSERT_TRUE(vm.call("setup"));
  TEST_ASSERT_TRUE(vm.call2("_dispatch_http_fail", "2", "0"));
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("0:nil", out.c_str());
}


namespace {
struct FakeMqtt : script::IScriptMqtt {
  std::string pubTopic, pubPayload;
  int publishes = 0;
  int subscribes = 0;
  void publish(const std::string& topic, const std::string& payload) override {
    pubTopic = topic;
    pubPayload = payload;
    ++publishes;
  }
  void subscribe(const std::string&) override { ++subscribes; }
  void unsubscribeAll(const std::string&) override {}
};
}

static void test_mqtt_publish_and_dispatch() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var seen = ''\n"
      "def setup()\n"
      "  mqtt.publish('a/b', 42)\n"
      "  mqtt.subscribe('t/x', def(topic, payload) seen = topic .. '=' .. payload end)\n"
      "end\n"
      "def draw() pixel(0, 0, seen == 't/x=hi' ? 0xCC : 0) end"));
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_EQUAL_INT(1, fake.publishes);
  TEST_ASSERT_EQUAL_STRING("a/b", fake.pubTopic.c_str());
  TEST_ASSERT_EQUAL_STRING("42", fake.pubPayload.c_str());
  TEST_ASSERT_EQUAL_INT(1, fake.subscribes);

  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call3("_dispatch_mqtt", "t/x", "t/x", "hi"));
    TEST_ASSERT_TRUE(vm.call3("_dispatch_mqtt", "other", "other", "hi"));
  }
  Canvas c(32, 8);
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0xCCu, c.getPixel(0, 0));
}

static void test_mqtt_wildcard_callback_receives_the_concrete_topic() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "var seen = ''\n"
      "def setup() mqtt.subscribe('sensor/#', def(topic, payload) seen = topic end) end\n"
      "def check() return seen end"));
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call3("_dispatch_mqtt", "sensor/#", "sensor/kitchen/temp", "21"));
  }
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("sensor/kitchen/temp", out.c_str());
}

static void test_mqtt_soft_fails_without_transport() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def setup()\n"
                                   "  mqtt.publish('a/b', 'x')\n"
                                   "  mqtt.subscribe('t', def(t, p) end)\n"
                                   "end"));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
}

static void test_mqtt_subscription_cap() {
  FakeMqtt fake;
  g_svc.mqtt = &fake;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def setup()\n"
                                   "  for i : 0 .. 11\n"
                                   "    mqtt.subscribe('t/' .. str(i), def(t, p) end)\n"
                                   "  end\n"
                                   "end"));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
  TEST_ASSERT_EQUAL_INT((int)script::kMaxMqttSubs, fake.subscribes);
}


static void test_num_accepts_numbers_bare_and_quoted() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  pixel(0, 0, num(42))\n"
                                   "  pixel(1, 0, int(num(3.5) * 2))\n"
                                   "  pixel(2, 0, num('5945') == 5945 ? 0x11 : 0)\n"
                                   "  pixel(3, 0, int(num('876.6') * 10))\n"
                                   "  pixel(4, 0, num('\"42\"') == 42 ? 0x22 : 0)\n"
                                   "  pixel(5, 0, int(num('\"876.6\"') * 10))\n"
                                   "  pixel(6, 0, num('-12') == -12 ? 0x33 : 0)\n"
                                   "end"));
  RenderCtx ctx;
  Canvas c(32, 8);
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(42u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x11u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(8766u & 0xFFFFFFu, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(8766u & 0xFFFFFFu, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(0x33u, c.getPixel(6, 0));
}

static void test_num_rejects_garbage_with_default() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  pixel(0, 0, num('abc') == nil ? 0x11 : 0)\n"
                                   "  pixel(1, 0, num('abc', 7))\n"
                                   "  pixel(2, 0, num(nil) == nil ? 0x22 : 0)\n"
                                   "  pixel(3, 0, num(nil, 9))\n"
                                   "  pixel(4, 0, num('876,6') == nil ? 0x33 : 0)\n"
                                   "  pixel(5, 0, num('876.6 W') == nil ? 0x44 : 0)\n"
                                   "  pixel(6, 0, num('true') == nil ? 0x55 : 0)\n"
                                   "  pixel(7, 0, num('[1,2]') == nil ? 0x66 : 0)\n"
                                   "  pixel(8, 0, num('{\"val\":1}') == nil ? 0x77 : 0)\n"
                                   "  pixel(9, 0, num('') == nil ? 0x88 : 0)\n"
                                   "end"));
  RenderCtx ctx;
  Canvas c(32, 8);
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(0x11u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(9u, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(0x33u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0x44u, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(0x55u, c.getPixel(6, 0));
  TEST_ASSERT_EQUAL_HEX32(0x66u, c.getPixel(7, 0));
  TEST_ASSERT_EQUAL_HEX32(0x77u, c.getPixel(8, 0));
  TEST_ASSERT_EQUAL_HEX32(0x88u, c.getPixel(9, 0));
}

static void test_round_is_half_away_from_zero() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  pixel(0, 0, round(2.5))\n"
                                   "  pixel(1, 0, round(2.4))\n"
                                   "  pixel(2, 0, round(-2.5) == -3 ? 0x11 : 0)\n"
                                   "  pixel(3, 0, round(-2.4) == -2 ? 0x22 : 0)\n"
                                   "  pixel(4, 0, round(7))\n"
                                   "  pixel(5, 0, int(round(876.64, 1) * 10))\n"
                                   "  pixel(6, 0, round('x') == nil ? 0x33 : 0)\n"
                                   "  pixel(7, 0, round(nil) == nil ? 0x44 : 0)\n"
                                   "end"));
  RenderCtx ctx;
  Canvas c(32, 8);
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(3u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(2u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x11u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(8766u & 0xFFFFFFu, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(0x33u, c.getPixel(6, 0));
  TEST_ASSERT_EQUAL_HEX32(0x44u, c.getPixel(7, 0));
}

static void test_clamp_min_max() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def draw()\n"
                                   "  pixel(0, 0, clamp(5, 0, 10))\n"
                                   "  pixel(1, 0, clamp(-3, 0, 10))\n"
                                   "  pixel(2, 0, clamp(99, 0, 10))\n"
                                   "  pixel(3, 0, min(2, 7))\n"
                                   "  pixel(4, 0, max(2, 7))\n"
                                   "  pixel(5, 0, min('a', 'b') == 'a' ? 0x11 : 0)\n"
                                   "  pixel(6, 0, int(clamp(0.5, 1.5, 9.0) * 2))\n"
                                   "end"));
  RenderCtx ctx;
  Canvas c(32, 8);
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(5u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(10u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(2u, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(0x11u, c.getPixel(5, 0));
  TEST_ASSERT_EQUAL_HEX32(3u, c.getPixel(6, 0));
}

static void test_store_roundtrip_in_vm() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def setup() store.set('n', 42) end\n"
                                   "def draw() pixel(0, 0, store.get('n', 0)) end"));
  RenderCtx ctx;
  Canvas c(32, 8);
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_TRUE(script::BindingScope::storeFlushPending());
  const script::BindingScope::StoreFlush f = script::BindingScope::takeStoreFlush();
  TEST_ASSERT_TRUE(f.json.find("42") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("T", f.script.c_str());
  TEST_ASSERT_FALSE(script::BindingScope::storeFlushPending());
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_HEX32(42u, c.getPixel(0, 0));
}

static void test_a_large_store_is_written() {
  std::string src = "def setup() store.set('k', '";
  src.append(6000, 'x');
  src += "') end";
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, src.c_str()));
  RenderCtx ctx;
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_TRUE(script::BindingScope::storeFlushPending());
  const script::BindingScope::StoreFlush f = script::BindingScope::takeStoreFlush();
  TEST_ASSERT_TRUE(f.json.size() > 6000u);
}

static void test_store_get_default_is_optional() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "var a = 'x'\nvar b = 'x'\nvar c = 'x'\n"
                                   "def setup()\n"
                                   "  a = store.get('missing')\n"
                                   "  b = store.get('missing', 7)\n"
                                   "  store.set('k', 'v')\n"
                                   "  c = store.get('k')\n"
                                   "end\n"
                                   "def draw()\n"
                                   "  pixel(0, 0, a == nil ? 0x11 : 0)\n"
                                   "  pixel(1, 0, b == 7 ? 0x22 : 0)\n"
                                   "  pixel(2, 0, c == 'v' ? 0x33 : 0)\n"
                                   "end"));
  RenderCtx ctx;
  Canvas c(32, 8);
  {
    script::BindingScope s(nullptr, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  {
    script::BindingScope s(&c, &ctx, "T");
    TEST_ASSERT_TRUE(vm.call("draw"));
  }
  TEST_ASSERT_EQUAL_HEX32(0x11u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(0x22u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(0x33u, c.getPixel(2, 0));
}

static void test_store_seed_via_store_load() {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE(script::installBindings(vm, err));
  {
    script::BindingScope s(nullptr, nullptr, "T");
    TEST_ASSERT_TRUE(vm.call1("_store_load", "{\"n\": 7}"));
  }
  TEST_ASSERT_TRUE(vm.load("def draw() pixel(0, 0, store.get('n', 0)) end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope s(&c, &ctx, "T");
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(0, 0));
}

static void test_store_over_budget_drop_is_logged() {
  std::string logged;
  g_svc.log = [&](const std::string& m) { logged = m; };
  script::heap::testing::setGrowthBudget(64);
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def setup()\n"
                                   "  var big = ''\n"
                                   "  for i : 0 .. 300 big = big .. '0123456789' end\n"
                                   "  store.set('big', big)\n"
                                   "end"));
  script::BindingScope::takeStoreFlush();
  {
    script::BindingScope s(nullptr, nullptr, "Weather");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  script::heap::testing::resetGrowthBudget();
  TEST_ASSERT_FALSE(script::BindingScope::storeFlushPending());
  TEST_ASSERT_TRUE(logged.find("[script:Weather]") != std::string::npos);
  TEST_ASSERT_TRUE(logged.find("store not saved") != std::string::npos);
  TEST_ASSERT_TRUE(logged.find(std::to_string(64)) != std::string::npos);
  g_svc.log = nullptr;
}

// What a script publishes is copied out of the Berry heap into the noticeboard, which nothing
// collects. Driven through the real binding rather than SharedState directly, because the
// binding is what a script reaches and false is what it has to see.
static void test_shared_set_refuses_a_write_that_no_longer_fits() {
  script::SharedState shared;
  g_svc.shared = &shared;
  script::heap::testing::setGrowthBudget(4096);
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "var taken = 0\n"
                                   "var refused = 0\n"
                                   "def setup()\n"
                                   "  var big = ''\n"
                                   "  for i : 0 .. 99 big = big .. '0123456789' end\n"
                                   "  for i : 0 .. 9\n"
                                   "    if shared.set('k' .. str(i), big)\n"
                                   "      taken = taken + 1\n"
                                   "    else\n"
                                   "      refused = refused + 1\n"
                                   "    end\n"
                                   "  end\n"
                                   "end\n"
                                   "def report() return str(taken) .. '/' .. str(refused) end"));
  {
    script::BindingScope s(nullptr, nullptr, "Weather");
    TEST_ASSERT_TRUE(vm.call("setup"));
    std::string out;
    TEST_ASSERT_TRUE(vm.callString("report", out));
    TEST_ASSERT_EQUAL_STRING("4/6", out.c_str());
  }
  script::heap::testing::resetGrowthBudget();
  TEST_ASSERT_EQUAL_UINT32(4u, static_cast<uint32_t>(shared.entries()));
  TEST_ASSERT_TRUE(shared.bytes("Weather") <= 4096u);
  g_svc.shared = nullptr;
}

static void test_store_load_ignores_non_map_json() {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE(script::installBindings(vm, err));
  TEST_ASSERT_TRUE(vm.call1("_store_load", "[1, 2]"));
  TEST_ASSERT_TRUE(vm.load("def check() return str(store.get('n', 7)) end"));
  script::BindingScope s(nullptr, nullptr, "T");
  std::string out;
  TEST_ASSERT_TRUE(vm.callString("check", out));
  TEST_ASSERT_EQUAL_STRING("7", out.c_str());
  TEST_ASSERT_TRUE(vm.load("def w() store.set('n', 3) end"));
  TEST_ASSERT_TRUE(vm.call("w"));
}

static void test_store_load_ignores_scalar_and_garbage_json() {
  const char* const blobs[] = {"42", "\"text\"", "true", "not json at all", ""};
  for (const char* blob : blobs) {
    script::BerryVM vm;
    std::string err;
    TEST_ASSERT_TRUE(script::installBindings(vm, err));
    TEST_ASSERT_TRUE_MESSAGE(vm.call1("_store_load", blob), blob);
    TEST_ASSERT_TRUE(vm.load("def check() return str(store.get('n', 7)) end"));
    script::BindingScope s(nullptr, nullptr, "T");
    std::string out;
    TEST_ASSERT_TRUE_MESSAGE(vm.callString("check", out), blob);
    TEST_ASSERT_EQUAL_STRING("7", out.c_str());
  }
}

static void test_store_flush_rejects_when_over_budget() {
  script::heap::testing::setGrowthBudget(64);
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm,
                                   "def setup()\n"
                                   "  var big = ''\n"
                                   "  for i : 0 .. 300 big = big .. '0123456789' end\n"
                                   "  store.set('big', big)\n"
                                   "end"));
  script::BindingScope::takeStoreFlush();
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
  script::heap::testing::resetGrowthBudget();
  TEST_ASSERT_FALSE(script::BindingScope::storeFlushPending());
}


static std::string g_logged;

static void test_log_is_tagged_with_script_name() {
  g_logged.clear();
  g_svc.log = [](const std::string& m) { g_logged = m; };
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "def setup() log('hello') end"));
  {
    script::BindingScope s(nullptr, nullptr, "Weather");
    TEST_ASSERT_TRUE(vm.call("setup"));
  }
  TEST_ASSERT_EQUAL_STRING("[script:Weather] hello", g_logged.c_str());
  g_svc.log = nullptr;
}

static void test_log_without_sink_is_silent() {
  g_svc.log = nullptr;
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(vm, "def setup() log('hello') end"));
  script::BindingScope s(nullptr, nullptr, "T");
  TEST_ASSERT_TRUE(vm.call("setup"));
}


static void test_unknown_builtin_fails_at_compile_time() {
  script::BerryVM vm;
  std::string err;
  TEST_ASSERT_TRUE(script::installBindings(vm, err));
  TEST_ASSERT_FALSE(vm.load("def draw() pixle(0,0,1) end"));
  TEST_ASSERT_TRUE(vm.lastError().find("syntax_error") != std::string::npos);
}


struct Engine {
  script::BerryVM vm;
  Engine() {
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(script::installBindings(vm, err), err.c_str());
  }
};

static std::string trace(script::ScriptApp& app) {
  std::string out;
  TEST_ASSERT_TRUE(app.callCheckForTest(out));
  return out;
}

static void test_lifecycle_sequence() {
  Engine e;
  script::ScriptApp app(e.vm, "L",
                        "class L\n"
                        "  var trace\n"
                        "  def init() self.trace = '' end\n"
                        "  def setup() self.trace += 'S' end\n"
                        "  def on_show() self.trace += '+' end\n"
                        "  def draw() self.trace += 'D' end\n"
                        "  def on_hide() self.trace += '-' end\n"
                        "  def loop() self.trace += 'L' end\n"
                        "  def on_button(b) self.trace += b end\n"
                        "  def check() return self.trace end\n"
                        "end\n"
                        "return L()",
                        script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  TEST_ASSERT_EQUAL_STRING("S", trace(app).c_str());

  Canvas c(32, 8);
  RenderCtx ctx;
  app.notifyVisible(true, &ctx);
  app.render(c, ctx);
  app.tickLoop(ctx);
  app.handleButton("select", &ctx);
  app.notifyVisible(false, &ctx);

  TEST_ASSERT_TRUE(app.ok());
  TEST_ASSERT_EQUAL_STRING("S+DLselect-", trace(app).c_str());
}

static void test_visibility_hooks_are_edge_triggered() {
  Engine e;
  script::ScriptApp app(e.vm, "V",
                        "class V\n"
                        "  var t\n"
                        "  def init() self.t = '' end\n"
                        "  def draw() end\n"
                        "  def on_show() self.t += '+' end\n"
                        "  def on_hide() self.t += '-' end\n"
                        "  def check() return self.t end\n"
                        "end\n"
                        "return V()",
                        script::ScriptMeta{}, "", nullptr);
  RenderCtx ctx;
  app.notifyVisible(true, &ctx);
  app.notifyVisible(true, &ctx);
  app.notifyVisible(false, &ctx);
  app.notifyVisible(false, &ctx);
  TEST_ASSERT_EQUAL_STRING("+-", trace(app).c_str());
}

static void test_button_ignored_while_hidden() {
  Engine e;
  script::ScriptApp app(e.vm, "B",
                        "class B\n"
                        "  var t\n"
                        "  def init() self.t = '' end\n"
                        "  def draw() end\n"
                        "  def on_button(b) self.t += b end\n"
                        "  def check() return self.t end\n"
                        "end\n"
                        "return B()",
                        script::ScriptMeta{}, "", nullptr);
  RenderCtx ctx;
  app.handleButton("left", &ctx);
  TEST_ASSERT_EQUAL_STRING("", trace(app).c_str());
  app.notifyVisible(true, &ctx);
  app.handleButton("left", &ctx);
  app.notifyVisible(false, &ctx);
  app.handleButton("right", &ctx);
  TEST_ASSERT_EQUAL_STRING("left", trace(app).c_str());
}

static void test_should_show_false_declines_the_turn() {
  Engine e;
  script::ScriptApp app(e.vm, "S",
                        "class S\n"
                        "  var on\n"
                        "  def init() self.on = false end\n"
                        "  def draw() end\n"
                        "  def should_show() return self.on end\n"
                        "  def loop() self.on = true end\n"
                        "end\n"
                        "return S()",
                        script::ScriptMeta{}, "", nullptr);
  RenderCtx ctx;
  TEST_ASSERT_FALSE(app.wantsShow(&ctx));
  TEST_ASSERT_FALSE(app.lastWantedShow());
  app.tickLoop(ctx);
  TEST_ASSERT_TRUE(app.wantsShow(&ctx));
  TEST_ASSERT_TRUE(app.lastWantedShow());
  TEST_ASSERT_TRUE(app.ok());
}

static void test_should_show_without_a_return_still_shows() {
  Engine e;
  script::ScriptApp app(e.vm, "N",
                        "class N\n"
                        "  var n\n"
                        "  def init() self.n = 0 end\n"
                        "  def draw() end\n"
                        "  def should_show() self.n = 1 end\n"
                        "  def check() return str(self.n) end\n"
                        "end\n"
                        "return N()",
                        script::ScriptMeta{}, "", nullptr);
  RenderCtx ctx;
  TEST_ASSERT_TRUE(app.wantsShow(&ctx));
  TEST_ASSERT_EQUAL_STRING("1", trace(app).c_str());
}

static void test_missing_should_show_shows() {
  Engine e;
  script::ScriptApp app(e.vm, "M", "class M def draw() end end\nreturn M()", script::ScriptMeta{}, "", nullptr);
  RenderCtx ctx;
  TEST_ASSERT_TRUE(app.wantsShow(&ctx));
  TEST_ASSERT_TRUE(app.lastWantedShow());
}

static void test_should_show_that_raises_leaves_the_app_visible() {
  Engine e;
  script::ScriptApp app(e.vm, "R",
                        "class R\n"
                        "  def draw() end\n"
                        "  def should_show() raise 'nope' end\n"
                        "end\n"
                        "return R()",
                        script::ScriptMeta{}, "", nullptr);
  RenderCtx ctx;
  TEST_ASSERT_TRUE(app.wantsShow(&ctx));
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_EQUAL_STRING("should_show", app.error().hook.c_str());
  TEST_ASSERT_TRUE(app.wantsShow(&ctx));
  TEST_ASSERT_TRUE(app.lastWantedShow());
}

static void test_missing_optional_hooks_are_fine() {
  Engine e;
  script::ScriptApp app(e.vm, "Min", "class Min def draw() end end\nreturn Min()", script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  Canvas c(32, 8);
  RenderCtx ctx;
  app.notifyVisible(true, &ctx);
  app.render(c, ctx);
  app.tickLoop(ctx);
  app.handleButton("left", &ctx);
  app.dispatchHttp(1, 200, "x", true, &ctx);
  app.dispatchMqtt("t", "t", "p", &ctx);
  TEST_ASSERT_TRUE(app.ok());
}

static void test_draw_required() {
  Engine e;
  script::ScriptApp app(e.vm, "NoDraw", "class NoDraw def setup() end end\nreturn NoDraw()",
                        script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_TRUE(app.error().message.find("draw") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("", app.error().hook.c_str());
  TEST_ASSERT_EQUAL_INT(0, app.error().line);
}

static void test_draw_not_required_when_headless() {
  Engine e;
  script::ScriptMeta meta;
  meta.headless = true;
  script::ScriptApp app(e.vm, "NoDraw", "class NoDraw def setup() end end\nreturn NoDraw()", meta,
                        "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
}

static void test_missing_return_latches() {
  Engine e;
  script::ScriptApp app(e.vm, "NoRet", "class NoRet def draw() end end", script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_TRUE(app.error().message.find("return") != std::string::npos);
}

static void test_compile_error_latches_at_construction() {
  Engine e;
  script::ScriptApp app(e.vm, "Syn", "class Syn def draw( end end\nreturn Syn()", script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_TRUE(app.error().message.find("syntax_error") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(1, app.error().line);
  TEST_ASSERT_TRUE(app.error().message.find("script:") == std::string::npos);
}

static void test_setup_failure_latches() {
  Engine e;
  script::ScriptApp app(e.vm, "S",
                        "class S\n"
                        "  var nil_fn\n"
                        "  def init() self.nil_fn = nil end\n"
                        "  def setup() self.nil_fn() end\n"
                        "  def draw() end\n"
                        "end\n"
                        "return S()",
                        script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_EQUAL_STRING("setup", app.error().hook.c_str());
  TEST_ASSERT_FALSE(app.error().empty());
}

static void test_error_latches_and_renders_err_frame() {
  Engine e;
  script::ScriptApp app(e.vm, "Bad",
                        "class Bad\n"
                        "  var n, nil_fn\n"
                        "  def init() self.n = 0 self.nil_fn = nil end\n"
                        "  def draw()\n"
                        "    self.n += 1\n"
                        "    pixel(0, 0, 0x123456)\n"
                        "    if self.n == 2 self.nil_fn() end\n"
                        "  end\n"
                        "end\n"
                        "return Bad()",
                        script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  Canvas c(32, 8);
  RenderCtx ctx;
  ctx.font = &kFont;

  app.render(c, ctx);
  TEST_ASSERT_TRUE(app.ok());
  TEST_ASSERT_EQUAL_HEX32(0x123456u, c.getPixel(0, 0));

  app.render(c, ctx);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_FALSE(app.error().empty());
  TEST_ASSERT_EQUAL_STRING("draw", app.error().hook.c_str());

  c.clear(0x00FF00u);
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(0u, c.getPixel(31, 7));
  int lit = 0;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      if (c.getPixel(x, y) == 0xFF0000u) ++lit;
  TEST_ASSERT_TRUE(lit > 0);

  const std::string before = app.error().message;
  const std::string beforeHook = app.error().hook;
  app.tickLoop(ctx);
  app.notifyVisible(true, &ctx);
  app.handleButton("select", &ctx);
  app.dispatchHttp(1, 200, "x", true, &ctx);
  app.dispatchMqtt("t", "t", "p", &ctx);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_EQUAL_STRING(before.c_str(), app.error().message.c_str());
  TEST_ASSERT_EQUAL_STRING(beforeHook.c_str(), app.error().hook.c_str());
}

static void test_instruction_limit_latches() {
  Engine e;
  script::ScriptApp app(e.vm, "Spin", "class Spin def draw() while true end end end\nreturn Spin()",
                        script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  Canvas c(32, 8);
  RenderCtx ctx;
  app.render(c, ctx);
  TEST_ASSERT_FALSE(app.ok());
  TEST_ASSERT_TRUE(app.error().message.find("instruction limit exceeded") != std::string::npos);
}

static void test_store_seed_loaded() {
  Engine e;
  script::ScriptApp app(e.vm, "St",
                        "class St def draw() pixel(0,0, store.get('n', 0)) end end\nreturn St()",
                        script::ScriptMeta{}, "{\"n\": 7}", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  Canvas c(32, 8);
  RenderCtx ctx;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(7u, c.getPixel(0, 0));
}

static void test_store_seed_visible_in_setup() {
  Engine e;
  script::ScriptApp app(e.vm, "St2",
                        "class St2\n"
                        "  var v\n"
                        "  def init() self.v = -1 end\n"
                        "  def setup() self.v = store.get('n', 0) end\n"
                        "  def draw() pixel(0, 0, self.v) end\n"
                        "end\n"
                        "return St2()",
                        script::ScriptMeta{}, "{\"n\": 9}", nullptr);
  Canvas c(32, 8);
  RenderCtx ctx;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(9u, c.getPixel(0, 0));
}

static void test_corrupt_store_seed_does_not_break_script() {
  Engine e;
  script::ScriptApp app(e.vm, "St3",
                        "class St3 def draw() pixel(0,0, store.get('n', 5)) end end\nreturn St3()",
                        script::ScriptMeta{}, "{not json", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  Canvas c(32, 8);
  RenderCtx ctx;
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(5u, c.getPixel(0, 0));
}

static void test_a_large_source_installs() {
  Engine e;
  std::string src = "class Big def draw() end end\nreturn Big()\n# ";
  src.append(40000, 'x');
  src += "\n";
  script::ScriptApp app(e.vm, "Big", src, script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
}

static void test_app_dispatches_http_to_its_own_callback() {
  Engine e;
  FakeHttp fake;
  g_svc.http = &fake;
  script::ScriptApp app(e.vm, "W",
                        "class W\n"
                        "  var t\n"
                        "  def init() self.t = -1 end\n"
                        "  def setup() http.get('http://x/', "
                        "def(b) self.t = b == nil ? 0 : int(b) end) end\n"
                        "  def draw() pixel(0, 0, self.t) end\n"
                        "end\n"
                        "return W()",
                        script::ScriptMeta{}, "", nullptr);
  TEST_ASSERT_TRUE(app.ok());
  Canvas c(32, 8);
  RenderCtx ctx;
  app.dispatchHttp(fake.lastId, 200, "33", true, &ctx);
  app.render(c, ctx);
  TEST_ASSERT_EQUAL_HEX32(33u, c.getPixel(0, 0));
}

static void test_two_apps_are_isolated() {
  Engine e;
  script::ScriptApp a(e.vm, "A",
                      "class App\n"
                      "  var shared, nil_fn\n"
                      "  def init() self.shared = 1 self.nil_fn = nil end\n"
                      "  def draw() self.nil_fn() end\n"
                      "end\n"
                      "return App()",
                      script::ScriptMeta{}, "", nullptr);
  script::ScriptApp b(e.vm, "B",
                      "class App\n"
                      "  var shared\n"
                      "  def init() self.shared = 2 end\n"
                      "  def draw() pixel(0, 0, self.shared) end\n"
                      "end\n"
                      "return App()",
                      script::ScriptMeta{}, "", nullptr);
  Canvas c(32, 8);
  RenderCtx ctx;
  a.render(c, ctx);
  TEST_ASSERT_FALSE(a.ok());
  b.render(c, ctx);
  TEST_ASSERT_TRUE(b.ok());
  TEST_ASSERT_EQUAL_HEX32(2u, c.getPixel(0, 0));
}

static void test_re_module_search_match_matchall() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(loadWithPrelude(
      vm,
      "def draw()\n"
      "  var m = re.search('value=(\\\\d+)', 'x value=42;')\n"
      "  if m != nil && m[0] == 'value=42' && m[1] == '42' pixel(0, 0, 1) end\n"
      "  if re.match('\\\\d+', 'a1') == nil pixel(1, 0, 2) end\n"
      "  var mm = re.match('(\\\\w+)', 'abc def')\n"
      "  if mm != nil && mm[1] == 'abc' pixel(2, 0, 3) end\n"
      "  var all = re.matchall('\\\\d+', 'a1 b22 c333')\n"
      "  if size(all) == 3 && all[2] == '333' pixel(3, 0, 4) end\n"
      "  if re.search('[', 'x') == nil pixel(4, 0, 5) end\n"
      "  var g = re.search('a(x)?b', 'ab')\n"
      "  if g != nil && g[1] == nil pixel(5, 0, 6) end\n"
      "end"));
  Canvas c(32, 8);
  RenderCtx ctx;
  script::BindingScope scope(&c, &ctx, "T");
  const bool ok = vm.call("draw");
  if (!ok) TEST_MESSAGE(vm.lastError().c_str());
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_HEX32(1u, c.getPixel(0, 0));
  TEST_ASSERT_EQUAL_HEX32(2u, c.getPixel(1, 0));
  TEST_ASSERT_EQUAL_HEX32(3u, c.getPixel(2, 0));
  TEST_ASSERT_EQUAL_HEX32(4u, c.getPixel(3, 0));
  TEST_ASSERT_EQUAL_HEX32(5u, c.getPixel(4, 0));
  TEST_ASSERT_EQUAL_HEX32(6u, c.getPixel(5, 0));
}

int main(int, char**) {
  static long t = 0;
  g_svc.monotonicMs = []() { return t += 10; };

  UNITY_BEGIN();
  RUN_TEST(test_draw_primitives);
  RUN_TEST(test_shape_primitives);
  RUN_TEST(test_draw_noop_without_canvas);
  RUN_TEST(test_width_height_zero_without_canvas);
  RUN_TEST(test_text_draws_with_real_font);
  RUN_TEST(test_text_width_matches_renderer);
  RUN_TEST(test_ink_width_reports_the_lit_pixels_not_the_advance);
  RUN_TEST(test_ink_width_reports_the_sentinel_without_a_font);
  RUN_TEST(test_text_noop_without_font);
  RUN_TEST(test_time_builtins_read_render_ctx);
  RUN_TEST(test_time_builtins_report_a_sentinel_without_a_clock);
  RUN_TEST(test_epoch_ms_reports_the_wall_clock);
  RUN_TEST(test_text_width_reports_the_sentinel_without_a_font);
  RUN_TEST(test_now_ms_uses_injected_clock);
  RUN_TEST(test_http_module_soft_fails_without_transport);
  RUN_TEST(test_http_dispatch_roundtrip);
  RUN_TEST(test_http_opts_cap_raises_the_response_limit);
  RUN_TEST(test_http_opts_cap_ignores_a_non_positive_value);
  RUN_TEST(test_http_failure_dispatch_delivers_nil);
  RUN_TEST(test_http_callback_fires_once);
  RUN_TEST(test_http_get_defaults_to_get_without_body_or_headers);
  RUN_TEST(test_http_post_carries_method_body_and_headers);
  RUN_TEST(test_http_verbs_reach_the_transport);
  RUN_TEST(test_http_rejects_bad_method_and_malformed_header_before_the_transport);
  RUN_TEST(test_http_callback_receives_the_status);
  RUN_TEST(test_mqtt_publish_and_dispatch);
  RUN_TEST(test_mqtt_wildcard_callback_receives_the_concrete_topic);
  RUN_TEST(test_mqtt_soft_fails_without_transport);
  RUN_TEST(test_mqtt_subscription_cap);
  RUN_TEST(test_num_accepts_numbers_bare_and_quoted);
  RUN_TEST(test_num_rejects_garbage_with_default);
  RUN_TEST(test_round_is_half_away_from_zero);
  RUN_TEST(test_clamp_min_max);
  RUN_TEST(test_store_roundtrip_in_vm);
  RUN_TEST(test_a_large_store_is_written);
  RUN_TEST(test_store_get_default_is_optional);
  RUN_TEST(test_store_seed_via_store_load);
  RUN_TEST(test_shared_set_refuses_a_write_that_no_longer_fits);
  RUN_TEST(test_store_load_ignores_non_map_json);
  RUN_TEST(test_store_load_ignores_scalar_and_garbage_json);
  RUN_TEST(test_store_flush_rejects_when_over_budget);
  RUN_TEST(test_store_over_budget_drop_is_logged);
  RUN_TEST(test_log_is_tagged_with_script_name);
  RUN_TEST(test_log_without_sink_is_silent);
  RUN_TEST(test_unknown_builtin_fails_at_compile_time);

  RUN_TEST(test_lifecycle_sequence);
  RUN_TEST(test_visibility_hooks_are_edge_triggered);
  RUN_TEST(test_button_ignored_while_hidden);
  RUN_TEST(test_should_show_false_declines_the_turn);
  RUN_TEST(test_should_show_without_a_return_still_shows);
  RUN_TEST(test_missing_should_show_shows);
  RUN_TEST(test_should_show_that_raises_leaves_the_app_visible);
  RUN_TEST(test_missing_optional_hooks_are_fine);
  RUN_TEST(test_draw_required);
  RUN_TEST(test_draw_not_required_when_headless);
  RUN_TEST(test_missing_return_latches);
  RUN_TEST(test_compile_error_latches_at_construction);
  RUN_TEST(test_setup_failure_latches);
  RUN_TEST(test_error_latches_and_renders_err_frame);
  RUN_TEST(test_instruction_limit_latches);
  RUN_TEST(test_store_seed_loaded);
  RUN_TEST(test_store_seed_visible_in_setup);
  RUN_TEST(test_corrupt_store_seed_does_not_break_script);
  RUN_TEST(test_a_large_source_installs);
  RUN_TEST(test_app_dispatches_http_to_its_own_callback);
  RUN_TEST(test_two_apps_are_isolated);
  RUN_TEST(test_re_module_search_match_matchall);
  return UNITY_END();
}
