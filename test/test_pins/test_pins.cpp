#include <unity.h>

#include <string>

#include "core/PinRules.h"

using namespace awtrix;

namespace {

const pins::SocProfile& esp32() { return pins::esp32Profile(); }
const pins::SocProfile& s3() { return pins::esp32s3Profile(); }

pins::PinSet ulanzi() { return esp32().defaults; }

pins::PinSet awtrix2() {
  pins::PinSet p;
  p.matrix = 21;
  p.btnLeft = 26;
  p.btnSelect = 16;
  p.btnRight = 5;
  p.battery = -1;
  p.ldr = 36;
  p.buzzer = -1;
  p.i2cSda = 17;
  p.i2cScl = 22;
  p.dfRx = 23;
  p.dfTx = 18;
  p.dfplayerEnabled = true;
  return p;
}

void test_i2s_is_offered_on_the_s3_only() {
  TEST_ASSERT_EQUAL_INT(-1, esp32().defaults.i2sBclk);
  TEST_ASSERT_EQUAL_INT(-1, esp32().defaults.i2sLrclk);
  TEST_ASSERT_EQUAL_INT(-1, esp32().defaults.i2sDout);
  TEST_ASSERT_EQUAL_INT(5, s3().defaults.i2sBclk);
  TEST_ASSERT_EQUAL_INT(6, s3().defaults.i2sLrclk);
  TEST_ASSERT_EQUAL_INT(4, s3().defaults.i2sDout);
}

void test_i2s_defaults_avoid_every_matrix_pin() {
  const pins::PinSet& d = s3().defaults;
  TEST_ASSERT_FALSE(pins::isMatrixPin(d.i2sBclk, s3()));
  TEST_ASSERT_FALSE(pins::isMatrixPin(d.i2sLrclk, s3()));
  TEST_ASSERT_FALSE(pins::isMatrixPin(d.i2sDout, s3()));
}

void test_mclk_and_amp_enable_are_off_by_default_and_share_no_pin() {
  std::string err;
  TEST_ASSERT_EQUAL_INT(-1, s3().defaults.i2sMclk);
  TEST_ASSERT_EQUAL_INT(-1, s3().defaults.ampEnable);
  TEST_ASSERT_EQUAL_INT(-1, esp32().defaults.i2sMclk);
  TEST_ASSERT_EQUAL_INT(-1, esp32().defaults.ampEnable);

  pins::PinSet p = s3().defaults;
  p.i2sMclk = 46;
  p.ampEnable = 45;
  TEST_ASSERT_TRUE(pins::validate(p, s3(), err));

  p.ampEnable = p.i2sMclk;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("duplicate") != std::string::npos);

  p = s3().defaults;
  p.ampEnable = 19;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("pinAmpEnable") != std::string::npos);
}

void test_positional_init_did_not_shift_the_other_pins() {
  TEST_ASSERT_EQUAL_INT(32, esp32().defaults.matrix);
  TEST_ASSERT_EQUAL_INT(15, esp32().defaults.buzzer);
  TEST_ASSERT_EQUAL_INT(18, esp32().defaults.dfTx);
  TEST_ASSERT_FALSE(esp32().defaults.dfplayerEnabled);
  TEST_ASSERT_EQUAL_INT(21, s3().defaults.matrix);
  TEST_ASSERT_EQUAL_INT(7, s3().defaults.buzzer);
  TEST_ASSERT_EQUAL_INT(18, s3().defaults.dfTx);
}

void test_i2s_pins_are_validated_like_any_other_output() {
  std::string err;
  pins::PinSet p = s3().defaults;
  p.i2sDout = 19;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("pinI2sDout") != std::string::npos);

  p = s3().defaults;
  p.i2sBclk = 23;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));

  p = s3().defaults;
  p.i2sLrclk = p.matrix;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("duplicate") != std::string::npos);
}

void test_i2s_pins_may_be_disabled_together() {
  std::string err;
  pins::PinSet p = s3().defaults;
  p.i2sBclk = p.i2sLrclk = p.i2sDout = -1;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, s3(), err), err.c_str());
}

void test_profile_defaults_are_valid() {
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(esp32().defaults, esp32(), err), err.c_str());
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(s3().defaults, s3(), err), err.c_str());
}

void test_every_matrix_pin_is_accepted_by_its_own_profile() {
  for (const pins::SocProfile* soc : {&esp32(), &s3()}) {
    for (std::size_t i = 0; i < soc->matrix.count; ++i) {
      pins::PinSet p = soc->defaults;
      p.matrix = soc->matrix.items[i];
      p.btnLeft = p.btnSelect = p.btnRight = -1;
      p.battery = p.ldr = p.buzzer = -1;
      p.i2cSda = p.i2cScl = p.dfRx = p.dfTx = -1;
      std::string err;
      TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, *soc, err), err.c_str());
    }
  }
}

void test_fallback_pin_is_a_member_of_the_active_list() {
  TEST_ASSERT_TRUE(pins::isMatrixPin(AWTRIX_MATRIX_FALLBACK_PIN, pins::activeProfile()));
  TEST_ASSERT_EQUAL_INT(pins::activeProfile().defaults.matrix, AWTRIX_MATRIX_FALLBACK_PIN);
}

void test_default_pin_set_is_inert() {
  pins::PinSet p;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
}

void test_duplicate_pins_rejected() {
  pins::PinSet p = ulanzi();
  p.buzzer = 26;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("duplicate") != std::string::npos);

  pins::PinSet q = s3().defaults;
  q.buzzer = q.i2cSda;
  TEST_ASSERT_FALSE(pins::validate(q, s3(), err));
  TEST_ASSERT_TRUE(err.find("duplicate") != std::string::npos);
}

void test_matrix_pin_conflict_names_the_matrix() {
  pins::PinSet p = ulanzi();
  p.buzzer = p.matrix;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("cannot be shared") != std::string::npos);
}

void test_disabled_pins_do_not_conflict() {
  pins::PinSet p = ulanzi();
  p.battery = -1;
  p.ldr = -1;
  p.buzzer = -1;
  p.btnLeft = -1;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, esp32(), err), err.c_str());
}

void test_awtrix2_preset_valid() {
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(awtrix2(), esp32(), err), err.c_str());
}

void test_esp32_matrix_pin_must_be_whitelisted() {
  pins::PinSet p = ulanzi();
  p.matrix = 17;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("pinMatrix") != std::string::npos);
}

void test_esp32_matrix_whitelist_members() {
  TEST_ASSERT_TRUE(pins::isMatrixPin(32, esp32()));
  TEST_ASSERT_TRUE(pins::isMatrixPin(21, esp32()));
  TEST_ASSERT_FALSE(pins::isMatrixPin(34, esp32()));
  TEST_ASSERT_FALSE(pins::isMatrixPin(-1, esp32()));
}

void test_esp32_input_only_rejected_for_outputs() {
  pins::PinSet p = ulanzi();
  p.buzzer = 35;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("input-only") != std::string::npos);
}

void test_esp32_buttons_reject_input_only() {
  pins::PinSet p = ulanzi();
  p.btnLeft = 39;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
}

void test_esp32_flash_pins_rejected() {
  pins::PinSet p = ulanzi();
  p.btnRight = 6;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("flash") != std::string::npos);
}

void test_esp32_out_of_range_gpio_rejected() {
  pins::PinSet p = ulanzi();
  p.btnRight = 40;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
}

void test_esp32_df_pins_validated_even_while_backend_off() {
  pins::PinSet p = ulanzi();
  p.buzzer = 23;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("duplicate pin 23") != std::string::npos);

  p.buzzer = 15;
  p.dfTx = 35;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("input-only") != std::string::npos);

  p.dfRx = -1;
  p.dfTx = -1;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, esp32(), err), err.c_str());
}

void test_s3_has_no_input_only_pins() {
  pins::PinSet p = s3().defaults;
  p.buzzer = 38;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, s3(), err), err.c_str());

  pins::PinSet q = ulanzi();
  q.buzzer = 38;
  TEST_ASSERT_FALSE(pins::validate(q, esp32(), err));
  TEST_ASSERT_TRUE(err.find("input-only") != std::string::npos);
}

void test_s3_unbonded_pins_rejected() {
  for (int pin = 22; pin <= 25; ++pin) {
    pins::PinSet p = s3().defaults;
    p.buzzer = pin;
    std::string err;
    TEST_ASSERT_FALSE_MESSAGE(pins::validate(p, s3(), err), std::to_string(pin).c_str());
  }
}

void test_s3_flash_and_psram_pins_rejected() {
  pins::PinSet p = s3().defaults;
  p.buzzer = 30;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("PSRAM") != std::string::npos);
}

void test_s3_usb_pins_rejected() {
  pins::PinSet p = s3().defaults;
  p.buzzer = 19;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("USB") != std::string::npos);
}

void test_s3_gpio_range_extends_past_39() {
  pins::PinSet p = s3().defaults;
  p.buzzer = 48;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, s3(), err), err.c_str());

  p.buzzer = 49;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
}

void test_adc1_follows_the_profile() {
  std::string err;

  pins::PinSet p = ulanzi();
  p.battery = 34;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(p, esp32(), err), err.c_str());

  pins::PinSet q = s3().defaults;
  q.battery = 34;
  TEST_ASSERT_FALSE(pins::validate(q, s3(), err));

  q.battery = 1;
  q.ldr = 2;
  TEST_ASSERT_TRUE_MESSAGE(pins::validate(q, s3(), err), err.c_str());

  p.battery = 1;
  TEST_ASSERT_FALSE(pins::validate(p, esp32(), err));
  TEST_ASSERT_TRUE(err.find("ADC1") != std::string::npos);
}

void test_rtc_wake_pins_follow_the_profile() {
  TEST_ASSERT_TRUE(pins::isRtcWakePin(27, esp32()));
  TEST_ASSERT_TRUE(pins::isRtcWakePin(0, esp32()));
  TEST_ASSERT_TRUE(pins::isRtcWakePin(39, esp32()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(16, esp32()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(21, esp32()));

  TEST_ASSERT_TRUE(pins::isRtcWakePin(21, s3()));
  TEST_ASSERT_TRUE(pins::isRtcWakePin(1, s3()));
  TEST_ASSERT_TRUE(pins::isRtcWakePin(17, s3()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(38, s3()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(39, s3()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(47, s3()));
}

void test_rtc_wake_pin_rejects_nonsense() {
  TEST_ASSERT_FALSE(pins::isRtcWakePin(-1, esp32()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(-1, s3()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(40, esp32()));
  TEST_ASSERT_FALSE(pins::isRtcWakePin(49, s3()));
}

void test_default_select_button_wakes_on_every_profile() {
  TEST_ASSERT_TRUE(pins::isRtcWakePin(esp32().defaults.btnSelect, esp32()));
  TEST_ASSERT_TRUE(pins::isRtcWakePin(s3().defaults.btnSelect, s3()));
}

void test_s3_rejects_classic_matrix_pins_it_cannot_drive() {
  pins::PinSet p = s3().defaults;
  p.matrix = 32;
  std::string err;
  TEST_ASSERT_FALSE(pins::validate(p, s3(), err));
  TEST_ASSERT_TRUE(err.find("pinMatrix") != std::string::npos);
}

}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_profile_defaults_are_valid);
  RUN_TEST(test_every_matrix_pin_is_accepted_by_its_own_profile);
  RUN_TEST(test_fallback_pin_is_a_member_of_the_active_list);
  RUN_TEST(test_default_pin_set_is_inert);
  RUN_TEST(test_duplicate_pins_rejected);
  RUN_TEST(test_matrix_pin_conflict_names_the_matrix);
  RUN_TEST(test_disabled_pins_do_not_conflict);

  RUN_TEST(test_awtrix2_preset_valid);
  RUN_TEST(test_esp32_matrix_pin_must_be_whitelisted);
  RUN_TEST(test_esp32_matrix_whitelist_members);
  RUN_TEST(test_esp32_input_only_rejected_for_outputs);
  RUN_TEST(test_esp32_buttons_reject_input_only);
  RUN_TEST(test_esp32_flash_pins_rejected);
  RUN_TEST(test_esp32_out_of_range_gpio_rejected);
  RUN_TEST(test_esp32_df_pins_validated_even_while_backend_off);

  RUN_TEST(test_s3_has_no_input_only_pins);
  RUN_TEST(test_s3_unbonded_pins_rejected);
  RUN_TEST(test_s3_flash_and_psram_pins_rejected);
  RUN_TEST(test_s3_usb_pins_rejected);
  RUN_TEST(test_s3_gpio_range_extends_past_39);
  RUN_TEST(test_adc1_follows_the_profile);
  RUN_TEST(test_s3_rejects_classic_matrix_pins_it_cannot_drive);
  RUN_TEST(test_rtc_wake_pins_follow_the_profile);
  RUN_TEST(test_rtc_wake_pin_rejects_nonsense);
  RUN_TEST(test_default_select_button_wakes_on_every_profile);
  RUN_TEST(test_i2s_is_offered_on_the_s3_only);
  RUN_TEST(test_i2s_defaults_avoid_every_matrix_pin);
  RUN_TEST(test_mclk_and_amp_enable_are_off_by_default_and_share_no_pin);
  RUN_TEST(test_positional_init_did_not_shift_the_other_pins);
  RUN_TEST(test_i2s_pins_are_validated_like_any_other_output);
  RUN_TEST(test_i2s_pins_may_be_disabled_together);
  UNITY_END();
  return 0;
}
