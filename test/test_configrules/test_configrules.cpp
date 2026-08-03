#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "core/ConfigRules.h"
#include "core/api/JsonReader.h"

using namespace awtrix;

namespace {

class Body {
 public:
  Body& set(const char* k, const char* v) { return add(k, quoted(v)); }
  Body& set(const char* k, int v) { return add(k, std::to_string(v)); }
  Body& set(const char* k, long v) { return add(k, std::to_string(v)); }
  Body& set(const char* k, double v) {
    char b[32];
    snprintf(b, sizeof(b), "%g", v);
    return add(k, b);
  }
  Body& set(const char* k, bool v) { return add(k, v ? "true" : "false"); }
  Body& raw(const char* k, const char* json) { return add(k, json); }
  std::string str() const { return "{" + body_ + "}"; }

 private:
  Body& add(const char* k, const std::string& v) {
    if (!body_.empty()) body_ += ',';
    body_ += std::string("\"") + k + "\":" + v;
    return *this;
  }
  static std::string quoted(const char* v) { return std::string("\"") + v + "\""; }
  std::string body_;
};

bool ok(const Body& d, cfgrules::ConfigError& e, bool allowEmptyClears = false) {
  return cfgrules::validateSystemRead(api::JsonReader(d.str()), e, allowEmptyClears);
}

void test_valid_payload_accepted() {
  Body d;
  d.set("mqttPort", 1883);
  d.set("webPort", 80);
  d.set("statsInterval", 10000);
  d.set("minBrightness", 10);
  d.set("maxBrightness", 220);
  d.set("batteryDividerRatio", 1.79);
  d.set("pinMatrix", 32);
  d.set("pinBattery", -1);
  d.set("hostname", "kitchen");
  cfgrules::ConfigError e;
  TEST_ASSERT_TRUE_MESSAGE(ok(d, e), e.field.c_str());
}

void test_out_of_range_rejected_with_field() {
  cfgrules::ConfigError e;
  Body a;
  a.set("maxBrightness", 999);
  TEST_ASSERT_FALSE(ok(a, e));
  TEST_ASSERT_EQUAL_STRING("maxBrightness", e.field.c_str());

  Body b;
  b.set("mqttPort", 0);
  TEST_ASSERT_FALSE(ok(b, e));
  TEST_ASSERT_EQUAL_STRING("mqttPort", e.field.c_str());

  Body c;
  c.set("statsInterval", 10);
  TEST_ASSERT_FALSE(ok(c, e));
  TEST_ASSERT_EQUAL_STRING("statsInterval", e.field.c_str());

  Body f;
  f.set("lowBatteryThreshold", 150);
  TEST_ASSERT_FALSE(ok(f, e));
  TEST_ASSERT_EQUAL_STRING("lowBatteryThreshold", e.field.c_str());
}

void test_type_mismatch_rejected() {
  cfgrules::ConfigError e;
  Body a;
  a.set("mqttPort", "1883");
  TEST_ASSERT_FALSE(ok(a, e));
  TEST_ASSERT_EQUAL_STRING("mqttPort", e.field.c_str());

  Body b;
  b.set("tempDecimals", 1.5);
  TEST_ASSERT_FALSE(ok(b, e));
  TEST_ASSERT_EQUAL_STRING("tempDecimals", e.field.c_str());
}

void test_pin_values_are_range_checked() {
  cfgrules::ConfigError e;
  Body a;
  a.set("pinBtnLeft", 99);
  TEST_ASSERT_FALSE(ok(a, e));
  TEST_ASSERT_EQUAL_STRING("pinBtnLeft", e.field.c_str());

  Body b;
  b.set("pinLdr", -2);
  TEST_ASSERT_FALSE(ok(b, e));
  TEST_ASSERT_EQUAL_STRING("pinLdr", e.field.c_str());
}

void test_unknown_keys_ignored() {
  Body d;
  d.set("somethingElse", 12345);
  cfgrules::ConfigError e;
  TEST_ASSERT_TRUE(ok(d, e));
}

void test_empty_string_on_load_bearing_field_rejected() {
  cfgrules::ConfigError e;
  Body c;
  c.set("wifiSsid", "");
  TEST_ASSERT_FALSE(ok(c, e));
  TEST_ASSERT_EQUAL_STRING("wifiSsid", e.field.c_str());
}

void test_gated_hosts_may_be_emptied_without_guard() {
  cfgrules::ConfigError e;
  Body a;
  a.set("mqttHost", "");
  a.set("authUser", "");
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e), e.field.c_str());
}

void test_empty_clears_permitted_when_allowed_for_restore() {
  cfgrules::ConfigError e;
  Body a;
  a.set("wifiSsid", "");
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e, true), e.field.c_str());
}

void test_range_checks_still_enforced_when_empty_clears_allowed() {
  cfgrules::ConfigError e;
  Body a;
  a.set("maxBrightness", 999);
  TEST_ASSERT_FALSE(ok(a, e, true));
  TEST_ASSERT_EQUAL_STRING("maxBrightness", e.field.c_str());
}

void test_empty_string_message_names_the_explicit_route() {
  cfgrules::ConfigError e;
  Body a;
  a.set("wifiSsid", "");
  TEST_ASSERT_FALSE(ok(a, e));
  TEST_ASSERT_NOT_NULL(strstr(e.message.c_str(), "factory-reset"));
}

void test_mqtt_gate_requires_host() {
  cfgrules::ConfigError e;
  TEST_ASSERT_FALSE(cfgrules::validateMqttGate(true, "", e));
  TEST_ASSERT_EQUAL_STRING("mqttHost", e.field.c_str());
  TEST_ASSERT_TRUE(cfgrules::validateMqttGate(true, "broker.local", e));
  TEST_ASSERT_TRUE(cfgrules::validateMqttGate(false, "", e));
}

void test_auth_gate_requires_user_and_pass() {
  cfgrules::ConfigError e;
  TEST_ASSERT_FALSE(cfgrules::validateAuthGate(true, "", "", e));
  TEST_ASSERT_EQUAL_STRING("authUser", e.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateAuthGate(true, "admin", "", e));
  TEST_ASSERT_FALSE(cfgrules::validateAuthGate(true, "", "secret", e));
  TEST_ASSERT_TRUE(cfgrules::validateAuthGate(true, "admin", "secret", e));
  TEST_ASSERT_TRUE(cfgrules::validateAuthGate(false, "", "", e));
}

void test_i2s_pins_are_all_or_none() {
  cfgrules::ConfigError e;
  TEST_ASSERT_TRUE(cfgrules::validateAudioPins(5, 6, 4, e));
  TEST_ASSERT_TRUE(cfgrules::validateAudioPins(-1, -1, -1, e));

  TEST_ASSERT_FALSE(cfgrules::validateAudioPins(-1, 6, 4, e));
  TEST_ASSERT_EQUAL_STRING("pinI2sBclk", e.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateAudioPins(5, -1, 4, e));
  TEST_ASSERT_EQUAL_STRING("pinI2sLrclk", e.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateAudioPins(5, 6, -1, e));
  TEST_ASSERT_EQUAL_STRING("pinI2sDout", e.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateAudioPins(5, -1, -1, e));
}

void test_non_empty_and_absent_load_bearing_fields_accepted() {
  cfgrules::ConfigError e;
  Body a;
  a.set("mqttHost", "192.168.1.10");
  a.set("authUser", "admin");
  a.set("wifiSsid", "home");
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e), e.field.c_str());

  Body b;
  b.set("mqttPort", 1883);
  TEST_ASSERT_TRUE(ok(b, e));
}

void test_other_strings_may_still_be_emptied() {
  cfgrules::ConfigError e;
  Body d;
  d.set("ntpServer", "");
  d.set("ip", "");
  d.set("mqttUser", "");
  TEST_ASSERT_TRUE_MESSAGE(ok(d, e), e.field.c_str());
}

void test_malformed_ip_fields_rejected() {
  const char* keys[] = {"ip", "gateway", "subnet", "dns1", "dns2"};
  for (const char* k : keys) {
    cfgrules::ConfigError e;
    Body d;
    d.set(k, "192.168.1.256");
    TEST_ASSERT_FALSE_MESSAGE(ok(d, e), k);
    TEST_ASSERT_EQUAL_STRING(k, e.field.c_str());
  }
  cfgrules::ConfigError e;
  for (const char* bad : {"192.168.1", "192.168.1.1.1", "192.168.1.a", "1.2.3.4 ", "...."}) {
    Body d;
    d.set("ip", bad);
    TEST_ASSERT_FALSE_MESSAGE(ok(d, e), bad);
  }
}

void test_valid_and_empty_ip_fields_accepted() {
  cfgrules::ConfigError e;
  Body a;
  a.set("ip", "192.168.1.50");
  a.set("gateway", "192.168.1.1");
  a.set("subnet", "255.255.255.0");
  a.set("dns1", "9.9.9.9");
  a.set("dns2", "0.0.0.0");
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e), e.field.c_str());

  Body b;
  b.set("ip", "");
  b.set("gateway", "");
  b.set("dns1", "");
  TEST_ASSERT_TRUE_MESSAGE(ok(b, e), e.field.c_str());
}

void test_ip_accepts_cidr_suffix() {
  cfgrules::ConfigError e;
  for (const char* good : {"192.168.1.50/0", "192.168.1.50/8", "192.168.1.50/24", "192.168.1.50/32"}) {
    Body d;
    d.set("ip", good);
    TEST_ASSERT_TRUE_MESSAGE(ok(d, e), good);
  }
  for (const char* bad : {"192.168.1.50/33", "192.168.1.50/", "192.168.1.50/1a", "192.168.1.50/024",
                          "192.168.1.50/24 ", "192.168.1.256/24", "/24", "192.168.1.50/24/24"}) {
    Body d;
    d.set("ip", bad);
    TEST_ASSERT_FALSE_MESSAGE(ok(d, e), bad);
    TEST_ASSERT_EQUAL_STRING("ip", e.field.c_str());
  }
}

void test_cidr_suffix_rejected_on_other_ip_fields() {
  cfgrules::ConfigError e;
  for (const char* k : {"gateway", "subnet", "dns1", "dns2"}) {
    Body d;
    d.set(k, "192.168.1.1/24");
    TEST_ASSERT_FALSE_MESSAGE(ok(d, e), k);
    TEST_ASSERT_EQUAL_STRING(k, e.field.c_str());
  }
}

void test_ip_suffix_and_subnet_together_rejected() {
  cfgrules::ConfigError e;
  Body a;
  a.set("ip", "192.168.1.50/24");
  a.set("subnet", "255.255.255.0");
  TEST_ASSERT_FALSE(ok(a, e));
  TEST_ASSERT_EQUAL_STRING("subnet", e.field.c_str());

  Body b;
  b.set("ip", "192.168.1.50/24");
  b.set("subnet", "");
  TEST_ASSERT_TRUE_MESSAGE(ok(b, e), e.field.c_str());

  Body c;
  c.set("ip", "192.168.1.50");
  c.set("subnet", "255.255.255.0");
  TEST_ASSERT_TRUE_MESSAGE(ok(c, e), e.field.c_str());
}

void test_ip_split_names_the_mask() {
  struct Case {
    const char* in;
    const char* ip;
    const char* mask;
  };
  for (const Case& c : {Case{"192.168.1.50/0", "192.168.1.50", "0.0.0.0"},
                        Case{"192.168.1.50/8", "192.168.1.50", "255.0.0.0"},
                        Case{"10.0.4.7/22", "10.0.4.7", "255.255.252.0"},
                        Case{"192.168.1.50/24", "192.168.1.50", "255.255.255.0"},
                        Case{"192.168.1.50/32", "192.168.1.50", "255.255.255.255"}}) {
    Body d;
    d.set("ip", c.in);
    const cfgrules::IpSplit split = cfgrules::systemIpSplit(api::JsonReader(d.str()));
    TEST_ASSERT_TRUE_MESSAGE(split.present, c.in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.ip, split.ip.c_str(), c.in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.mask, split.subnet.c_str(), c.in);
  }
}

void test_ip_split_is_absent_without_a_suffix() {
  for (const char* body : {R"({"ip":"192.168.1.50","subnet":"255.255.255.0"})", R"({"ip":""})",
                           R"({"hostname":"kitchen"})", R"({"ip":42})", R"({})", "[]",
                           R"({"ip":"192.168.1.50/99"})", R"({"ip":"/24"})"}) {
    TEST_ASSERT_FALSE_MESSAGE(cfgrules::systemIpSplit(api::JsonReader(body)).present, body);
  }
}

void test_static_ip_without_mask_rejected() {
  cfgrules::ConfigError e;
  TEST_ASSERT_FALSE(cfgrules::validateStaticNet(true, "192.168.1.50", "", e));
  TEST_ASSERT_EQUAL_STRING("subnet", e.field.c_str());

  TEST_ASSERT_TRUE(cfgrules::validateStaticNet(true, "192.168.1.50", "255.255.255.0", e));
  TEST_ASSERT_TRUE(cfgrules::validateStaticNet(true, "", "", e));
  TEST_ASSERT_TRUE(cfgrules::validateStaticNet(false, "192.168.1.50", "", e));
}

void test_inverted_brightness_window_rejected() {
  cfgrules::ConfigError e;
  TEST_ASSERT_FALSE(cfgrules::validateBrightnessWindow(200, 100, e));
  TEST_ASSERT_EQUAL_STRING("minBrightness", e.field.c_str());

  TEST_ASSERT_TRUE(cfgrules::validateBrightnessWindow(10, 220, e));
  TEST_ASSERT_TRUE(cfgrules::validateBrightnessWindow(120, 120, e));
}

void test_panel_width_product_must_fit_the_envelope() {
  cfgrules::ConfigError e;
  TEST_ASSERT_TRUE(cfgrules::validateMatrixGeometry(32, 1, e));
  TEST_ASSERT_TRUE(cfgrules::validateMatrixGeometry(8, 4, e));
  TEST_ASSERT_TRUE(cfgrules::validateMatrixGeometry(128, 1, e));
  TEST_ASSERT_TRUE(cfgrules::validateMatrixGeometry(33, 1, e));

  TEST_ASSERT_FALSE(cfgrules::validateMatrixGeometry(16, 1, e));
  TEST_ASSERT_EQUAL_STRING("panelWidth", e.field.c_str());
  TEST_ASSERT_FALSE(cfgrules::validateMatrixGeometry(128, 2, e));
  TEST_ASSERT_EQUAL_STRING("panelWidth", e.field.c_str());
}

void test_panel_fields_are_range_checked() {
  cfgrules::ConfigError e;
  Body a;
  a.set("panelWidth", 129);
  TEST_ASSERT_FALSE(ok(a, e));
  TEST_ASSERT_EQUAL_STRING("panelWidth", e.field.c_str());

  Body b;
  b.set("panels", 0);
  TEST_ASSERT_FALSE(ok(b, e));
  TEST_ASSERT_EQUAL_STRING("panels", e.field.c_str());

  Body c;
  c.set("panels", 129);
  TEST_ASSERT_FALSE(ok(c, e));
  TEST_ASSERT_EQUAL_STRING("panels", e.field.c_str());

  Body d;
  d.set("panelWidth", 8);
  d.set("panels", 4);
  TEST_ASSERT_TRUE_MESSAGE(ok(d, e), e.field.c_str());
}

void test_wiring_enums_are_named_not_numbered() {
  cfgrules::ConfigError e;
  Body a;
  a.set("panelStart", "bottomRight");
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e), e.field.c_str());

  Body b;
  b.set("panelStart", "TOPLEFT");
  TEST_ASSERT_TRUE_MESSAGE(ok(b, e), e.field.c_str());

  Body c;
  c.set("panelStart", "sideways");
  TEST_ASSERT_FALSE(ok(c, e));
  TEST_ASSERT_EQUAL_STRING("panelStart", e.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be one of: topLeft topRight bottomLeft bottomRight",
                           e.message.c_str());

  Body d;
  d.set("panelStart", 2);
  TEST_ASSERT_FALSE(ok(d, e));
  TEST_ASSERT_EQUAL_STRING("panelStart", e.field.c_str());

  Body f;
  f.set("panelWiring", "columns");
  TEST_ASSERT_TRUE_MESSAGE(ok(f, e), e.field.c_str());

  Body g;
  g.set("panelWiring", "diagonal");
  TEST_ASSERT_FALSE(ok(g, e));
  TEST_ASSERT_EQUAL_STRING("must be one of: rows columns", e.message.c_str());
}

void test_wiring_booleans_must_be_booleans() {
  cfgrules::ConfigError e;
  Body a;
  a.set("panelSerpentine", false);
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e), e.field.c_str());

  Body b;
  b.set("panelSerpentine", "flase");
  TEST_ASSERT_FALSE(ok(b, e));
  TEST_ASSERT_EQUAL_STRING("panelSerpentine", e.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be a boolean", e.message.c_str());

  Body r;
  r.set("panelChainReverse", true);
  TEST_ASSERT_TRUE_MESSAGE(ok(r, e), e.field.c_str());

  Body rb;
  rb.set("panelChainReverse", "yes");
  TEST_ASSERT_FALSE(ok(rb, e));
  TEST_ASSERT_EQUAL_STRING("panelChainReverse", e.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be a boolean", e.message.c_str());

  Body s;
  s.set("panelChainSerpentine", true);
  TEST_ASSERT_TRUE_MESSAGE(ok(s, e), e.field.c_str());

  Body sb;
  sb.set("panelChainSerpentine", "yes");
  TEST_ASSERT_FALSE(ok(sb, e));
  TEST_ASSERT_EQUAL_STRING("panelChainSerpentine", e.field.c_str());
  TEST_ASSERT_EQUAL_STRING("must be a boolean", e.message.c_str());

  Body c;
  c.set("rotate", 1);
  TEST_ASSERT_FALSE(ok(c, e));
  TEST_ASSERT_EQUAL_STRING("rotate", e.field.c_str());

  Body d;
  d.set("mirror", true);
  d.set("rotate", false);
  TEST_ASSERT_TRUE_MESSAGE(ok(d, e), e.field.c_str());
}

void test_script_limit_range_checked() {
  cfgrules::ConfigError e;
  Body a;
  a.set("scriptLimit", 0);
  TEST_ASSERT_TRUE_MESSAGE(ok(a, e), e.field.c_str());

  Body b;
  b.set("scriptLimit", 6);
  TEST_ASSERT_TRUE_MESSAGE(ok(b, e), e.field.c_str());

  Body c;
  c.set("scriptLimit", 33);
  TEST_ASSERT_FALSE(ok(c, e));
  TEST_ASSERT_EQUAL_STRING("scriptLimit", e.field.c_str());

  Body d;
  d.set("scriptLimit", -1);
  TEST_ASSERT_FALSE(ok(d, e));
  TEST_ASSERT_EQUAL_STRING("scriptLimit", e.field.c_str());

  Body f;
  f.set("scriptLimit", 2.5);
  TEST_ASSERT_FALSE(ok(f, e));
  TEST_ASSERT_EQUAL_STRING("scriptLimit", e.field.c_str());
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_payload_accepted);
  RUN_TEST(test_out_of_range_rejected_with_field);
  RUN_TEST(test_type_mismatch_rejected);
  RUN_TEST(test_pin_values_are_range_checked);
  RUN_TEST(test_unknown_keys_ignored);
  RUN_TEST(test_empty_string_on_load_bearing_field_rejected);
  RUN_TEST(test_gated_hosts_may_be_emptied_without_guard);
  RUN_TEST(test_empty_clears_permitted_when_allowed_for_restore);
  RUN_TEST(test_range_checks_still_enforced_when_empty_clears_allowed);
  RUN_TEST(test_empty_string_message_names_the_explicit_route);
  RUN_TEST(test_mqtt_gate_requires_host);
  RUN_TEST(test_auth_gate_requires_user_and_pass);
  RUN_TEST(test_non_empty_and_absent_load_bearing_fields_accepted);
  RUN_TEST(test_other_strings_may_still_be_emptied);
  RUN_TEST(test_malformed_ip_fields_rejected);
  RUN_TEST(test_valid_and_empty_ip_fields_accepted);
  RUN_TEST(test_ip_accepts_cidr_suffix);
  RUN_TEST(test_cidr_suffix_rejected_on_other_ip_fields);
  RUN_TEST(test_ip_suffix_and_subnet_together_rejected);
  RUN_TEST(test_ip_split_names_the_mask);
  RUN_TEST(test_ip_split_is_absent_without_a_suffix);
  RUN_TEST(test_static_ip_without_mask_rejected);
  RUN_TEST(test_inverted_brightness_window_rejected);
  RUN_TEST(test_panel_width_product_must_fit_the_envelope);
  RUN_TEST(test_panel_fields_are_range_checked);
  RUN_TEST(test_wiring_enums_are_named_not_numbered);
  RUN_TEST(test_wiring_booleans_must_be_booleans);
  RUN_TEST(test_script_limit_range_checked);
  RUN_TEST(test_i2s_pins_are_all_or_none);
  UNITY_END();
  return 0;
}
