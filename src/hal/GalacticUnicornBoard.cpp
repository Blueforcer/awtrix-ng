#include "hal/GalacticUnicornBoard.h"
#include "platform/rp2040/GalacticUnicornPio.h"
#include <Arduino.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/adc.h>
#include <pico/time.h>

namespace awtrix {
GalacticUnicornBoard::GalacticUnicornBoard(const DeviceConfig& cfg)
    : height_(galactic::sanitizeHeight(cfg.panelHeight)),
      invalidHeight_(cfg.panelHeight != height_) {}

void GalacticUnicornBoard::begin() {
  using namespace galactic;
  if (ready_) return;
  // Keep the amp silent throughout boot, before radio/audio initialization.
  pinMode(22, OUTPUT);
  digitalWrite(22, LOW);
  for (const auto& input : Inputs) {
    gpio_init(input.pin);
    gpio_set_dir(input.pin, GPIO_IN);
    gpio_pull_up(input.pin);
  }
  adc_init();
  adc_gpio_init(LightSensor);
  if (invalidHeight_) Serial.println("warning: Galactic Unicorn panelHeight must be 8 or 11; using 11");
  // Keep blank asserted and select an invisible row throughout initialization/failure.
  for (uint pin = ColumnClock; pin <= Row3; ++pin) {
    gpio_init(pin);
    gpio_put(pin, pin >= ColumnBlank);
    gpio_set_dir(pin, GPIO_OUT);
  }
  // Prefer PIO1, leaving PIO0's instruction RAM for CYW43. Both the program
  // capacity and the SDK's shared SM allocation bitmap are checked dynamically.
  for (PIO candidate : {pio1, pio0}) {
    if (!pio_can_add_program(candidate, &program)) continue;
    const int sm = pio_claim_unused_sm(candidate, false);
    if (sm >= 0) { pio_ = candidate; sm_ = sm; break; }
  }
  if (!pio_) { Serial.println("display: no free PIO program/SM; panel remains blank"); return; }
  dataDma_ = dma_claim_unused_channel(false);
  controlDma_ = dma_claim_unused_channel(false);
  if (dataDma_ < 0 || controlDma_ < 0) {
    if (dataDma_ >= 0) dma_channel_unclaim(dataDma_);
    if (controlDma_ >= 0) dma_channel_unclaim(controlDma_);
    pio_sm_unclaim(pio_, sm_);
    pio_ = nullptr;
    Serial.println("display: two DMA channels unavailable; panel remains blank");
    return;
  }
  const uint offset = pio_add_program(pio_, &program);
  sleep_ms(100);
  // Pimoroni's FM6126 initialization: ten chips, last eleven clocks latched.
  constexpr uint16_t reg = 0xffce;
  for (int chip = 0; chip < 10; ++chip) {
    for (int bit = 0; bit < 16; ++bit) {
      gpio_put(ColumnData, reg & (1u << (15 - bit)));
      sleep_us(10);
      gpio_put(ColumnClock, true);
      sleep_us(10);
      gpio_put(ColumnClock, false);
      if (chip == 9 && bit == 4) gpio_put(ColumnLatch, true);
    }
  }
  gpio_put(ColumnLatch, false);
  gpio_put(ColumnBlank, false);
  sleep_us(10);
  gpio_put(ColumnBlank, true);
  for (uint pin = ColumnClock; pin <= Row3; ++pin) pio_gpio_init(pio_, pin);
  const uint safePins = (1u << ColumnBlank) | (15u << Row0);
  pio_sm_set_pins_with_mask(pio_, sm_, safePins, safePins);
  pio_sm_set_consecutive_pindirs(pio_, sm_, ColumnClock, 8, true);
  auto c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, offset, offset + 23);
  sm_config_set_sideset(&c, 2, true, false);
  sm_config_set_out_shift(&c, true, true, 32);
  sm_config_set_out_pins(&c, Row0, 4);
  sm_config_set_set_pins(&c, ColumnData, 3);
  sm_config_set_sideset_pins(&c, ColumnClock);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  // Same timing on Pico W and Pico 2 W (upstream ran at 125 MHz).
  sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / 125000000.0f);
  pio_sm_init(pio_, sm_, offset, &c);
  clearStream(buffers_[0]);
  clearStream(buffers_[1]);
  updateGrade();
  nextStream_ = reinterpret_cast<uintptr_t>(buffers_[0].bytes.data());
  auto ctrl = dma_channel_get_default_config(controlDma_);
  channel_config_set_transfer_data_size(&ctrl, DMA_SIZE_32);
  channel_config_set_read_increment(&ctrl, false);
  channel_config_set_write_increment(&ctrl, false);
  channel_config_set_chain_to(&ctrl, dataDma_);
  dma_channel_configure(controlDma_, &ctrl, &dma_hw->ch[dataDma_].read_addr,
                        &nextStream_, 1, false);
  auto data = dma_channel_get_default_config(dataDma_);
  channel_config_set_transfer_data_size(&data, DMA_SIZE_32);
  channel_config_set_read_increment(&data, true);
  channel_config_set_write_increment(&data, false);
  channel_config_set_dreq(&data, pio_get_dreq(pio_, sm_, true));
  channel_config_set_chain_to(&data, controlDma_);
  dma_channel_configure(dataDma_, &data, &pio_->txf[sm_], nullptr, StreamBytes / 4, false);
  __dmb();
  pio_sm_set_enabled(pio_, sm_, true);
  dma_start_channel_mask(1u << controlDma_);
  ready_ = true;
  Serial.printf("display: PIO%u SM%d DMA%d+%d; CYW43 not initialized (network pending)\n",
                pio_ == pio0 ? 0u : 1u, sm_, dataDma_, controlDma_);
}

void GalacticUnicornBoard::logRefreshProgress() const {
  if (!ready_) { Serial.println("display: refresh unavailable after WiFi init"); return; }
  const auto before = dma_hw->ch[dataDma_].read_addr;
  const auto start = time_us_64();
  bool advanced = false;
  while (time_us_64() - start < 20000) {
    if (dma_hw->ch[dataDma_].read_addr != before) { advanced = true; break; }
    tight_loop_contents();
  }
  Serial.printf("display: PIO%u SM%d DMA%d+%d after WiFi: refresh %s\n",
                pio_get_index(pio_), sm_, dataDma_, controlDma_,
                advanced ? "advancing" : "NOT advancing");
}

int GalacticUnicornBoard::readLdrRaw() {
  adc_select_input(galactic::LightAdc);
  return adc_read(); // Native 12-bit counts, same 0..4095 curve as ESP32.
}

std::array<bool, 9> GalacticUnicornBoard::readInputs() const {
  std::array<bool, 9> result{};
  for (size_t i = 0; i < result.size(); ++i) result[i] = !gpio_get(galactic::Inputs[i].pin);
  return result;
}

void GalacticUnicornBoard::pollButtons(ButtonState& out) {
  const auto inputs = readInputs();
  out = {inputs[0], inputs[1], inputs[2]}; // Debounced by the shared PeripheryService.
}

void GalacticUnicornBoard::show(const Canvas& canvas) {
  if (!ready_) return;
  const int back = 1 - front_;
  galactic::packCanvas(buffers_[back], canvas, height_, grade_, orientation_);
  const uintptr_t begin = reinterpret_cast<uintptr_t>(buffers_[back].bytes.data());
  __dmb(); // Finish all pixel writes before the control DMA can publish the buffer.
  nextStream_ = begin;
  __dmb();
  // The control DMA latches the new base ONLY after the previous full frame.
  // Wait for a read inside the new buffer before allowing the next show() to
  // reuse the old one. PIO may still contain old tail words, but they are already
  // copied into its FIFO. No refresh IRQ or CPU servicing is required.
  for (;;) {
    const uintptr_t reading = dma_hw->ch[dataDma_].read_addr;
    if (reading > begin && reading <= begin + galactic::StreamBytes) break;
    tight_loop_contents();
  }
  __dmb();
  front_ = back;
}
}
