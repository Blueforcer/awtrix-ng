#include "../../support.h"
#include "platform/tc002/runtime/Tc002Board.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

using awtrix::test::require;

struct State {
  bool acquire = true;
  int acquisitions = 0;
  int releases = 0;
  int measuredTransfers = 0;
  int failTransfer = -1;
  std::ptrdiff_t failResult = -1;
  std::vector<std::vector<uint8_t>> frames;
  std::vector<uint8_t> buffered, visible;
};

class FakeIo final : public awtrix::Tc002PanelIo {
 public:
  explicit FakeIo(std::shared_ptr<State> state) : state_(std::move(state)) {}
  bool acquire(std::string& error) override {
    ++state_->acquisitions;
    if (!state_->acquire) error = "lock TC002 SPI: busy";
    return state_->acquire;
  }
  void setTiming(awtrix::LinuxPanelTiming* timing) override { timing_ = timing; }
  std::ptrdiff_t transfer(const uint8_t* data, std::size_t size, std::string& error) override {
    if (timing_ && timing_->active) ++state_->measuredTransfers;
    const int count = static_cast<int>(state_->frames.size());
    state_->frames.emplace_back(data, data + size);
    if (count != state_->failTransfer) {
      state_->visible = state_->buffered;
      state_->buffered = state_->frames.back();
      return static_cast<std::ptrdiff_t>(size);
    }
    if (state_->failResult < 0) error = "injected SPI failure";
    return state_->failResult;
  }
  void release() noexcept override { ++state_->releases; }
 private:
  std::shared_ptr<State> state_;
  awtrix::LinuxPanelTiming* timing_ = nullptr;
};

std::unique_ptr<FakeIo> fake(const std::shared_ptr<State>& state) {
  return std::make_unique<FakeIo>(state);
}

bool black(const std::vector<uint8_t>& frame) {
  return frame.size() == awtrix::Tc002Board::kFrameBytes &&
         std::all_of(frame.begin(), frame.end(), [](uint8_t value) { return value == 0; });
}

// The code whose measured light is nearest; among codes of equal light, the lowest.
int nearestCode(long light16) {
  const auto& table = awtrix::Tc002Board::kPanelLight;
  int best = 0;
  for (int code = 1; code < 256; ++code)
    if (std::labs(table[code] - light16) < std::labs(table[best] - light16)) best = code;
  return best;
}

long linearLight(int value) { return std::lround(value / 255.0 * 65535.0); }

awtrix::render::GradeParams linearGrade() {
  awtrix::render::GradeParams grade;
  grade.gamma = 1.0f;
  return grade;
}

void packingAndShutdown() {
  auto state = std::make_shared<State>();
  {
    awtrix::Tc002Board board(fake(state));
    require(!board.displayReady(), "uninitialized panel is unavailable");
    require(board.matrixWidth() == 52 && board.matrixHeight() == 16, "physical dimensions");
    require(!board.hasBattery() && !board.hasLightSensor() && !board.sensors().hasSensor(),
            "unmeasured hardware stays unavailable");
    require(!board.toneSink() && !board.trackSink(), "audio is unavailable");
    board.begin();
    require(board.displayReady() && board.error().empty(), "successful transport opens panel");
    require(state->frames.size() == 2 && black(state->frames[0]) && black(state->frames[1]),
            "initialization clears both MCU buffers");
    board.begin();
    require(state->acquisitions == 1 && state->frames.size() == 2, "begin is idempotent");
    board.applyColorGrade(linearGrade());
    board.setBrightness(255);
    awtrix::Canvas canvas(52, 16);
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 52; ++x)
        canvas.setPixel(x, y, ((x + y) % 3 == 0) ? 0xFF0000u :
                              ((x + y) % 3 == 1) ? 0x008000u : 0x0000FFu);
    board.show(canvas);
    require(state->frames.size() == 3 && black(state->visible) && state->buffered == state->frames[2],
            "one show submits exactly one full frame with one-frame visibility latency");
    const auto& frame = state->frames[2];
    require(frame.size() == 3072, "complete physical SPI frame size");
    for (int y = 0; y < 16; ++y) {
      for (int x = 0; x < 52; ++x) {
        const auto offset = y * 192 + x * 3;
        const auto channel = (x + y) % 3;
        require(frame[offset] == (channel == 0 ? 255 : 0) &&
                frame[offset + 1] == (channel == 1 ? nearestCode(linearLight(0x80)) : 0) &&
                frame[offset + 2] == (channel == 2 ? 255 : 0), "all 832 pixels map row-major RGB");
      }
      for (int offset = 156; offset < 192; ++offset)
        require(frame[y * 192 + offset] == 0, "unused columns are zero padded on every row");
    }
    require(state->releases == 0, "transport retained while display is active");
  }
  require(state->releases == 1, "destructor releases panel once");
  require(state->frames.size() == 5 && black(state->frames[3]) && black(state->frames[4]) &&
          black(state->visible) && black(state->buffered),
          "destructor blanks both MCU buffers before release");
}

void continuousPipelineAndStillContent() {
  auto state = std::make_shared<State>();
  {
    awtrix::Tc002Board board(fake(state));
    board.begin();
    board.applyColorGrade(linearGrade());
    require(black(state->visible) && black(state->buffered), "initialization flushes both model buffers");
    awtrix::Canvas canvas(52, 16);
    const uint32_t colors[]{0xFF0000, 0x00FF00, 0x0000FF, 0x0000FF, 0x0000FF};
    for (std::size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
      const auto previous = state->buffered;
      canvas.clear(colors[i]);
      board.show(canvas);
      require(state->frames.size() == i + 3, "every rapid or unchanged show transfers once");
      require(state->visible == previous && state->buffered == state->frames.back(),
              "continuous output exposes the preceding complete frame in order");
      if (i >= 3) require(state->visible == state->buffered, "consecutive still shows finish the pending image");
    }
  }
  require(state->frames.size() == 9 && black(state->visible) && black(state->buffered),
          "final double black flushes a previously nonblack pipeline");
}

void brightnessAndGrade() {
  auto state = std::make_shared<State>();
  awtrix::Tc002Board board(fake(state));
  board.begin();
  board.applyColorGrade(linearGrade());
  awtrix::Canvas canvas(52, 16);
  canvas.clear(0xFFFFFFu);
  board.show(canvas);
  require(black(state->frames.back()), "nothing lights before the runtime sets a brightness");
  int previous = 0;
  for (int requested = 0; requested <= 255; ++requested) {
    board.setBrightness(static_cast<uint8_t>(requested));
    board.show(canvas);
    const auto& frame = state->frames.back();
    const int code = frame[0];
    require(frame[1] == code && frame[2] == code, "white keeps its channels equal");
    const int nearest = nearestCode(65535L * requested / 255);
    require(code == (requested == 0 ? 0 : std::max(nearest, 50)),
            "white becomes the nearest measured level and dimming never turns it off");
    require(code >= previous, "panel codes never fall as brightness rises");
    previous = code;
  }
  require(previous == 255, "full brightness reaches the top panel code");
  for (int value = 0; value <= 255; ++value) {
    canvas.clear(static_cast<uint32_t>(value) << 16);
    board.show(canvas);
    require(state->frames.back()[0] == nearestCode(linearLight(value)),
            "each level becomes the code whose measured light is nearest");
  }
  canvas.clear(0xFFFFFFu);
  auto grade = linearGrade();
  grade.correction = 0x800000u;
  board.applyColorGrade(grade);
  board.show(canvas);
  const auto& corrected = state->frames.back();
  require(corrected[0] > 50 && corrected[0] < 255 && corrected[1] == 0 && corrected[2] == 0,
          "shared color grade precedes wire encoding");
  board.setBrightness(0);
  board.show(canvas);
  require(black(state->frames.back()), "zero brightness remains black after correction");
}

void failuresCloseWithoutMoreWrites() {
  for (const auto failure : {-1, 0, 3071}) {
    for (const int failedFrame : {0, 1, 2, 3}) {
      auto state = std::make_shared<State>();
      state->failTransfer = failedFrame;
      state->failResult = failure;
      {
        awtrix::Tc002Board board(fake(state));
        board.begin();
        awtrix::Canvas canvas(52, 16);
        board.show(canvas);
        board.show(canvas); // Exercise failures in either successive one-transfer runtime show.
        require(!board.displayReady() && !board.error().empty(), "short/failed transfer disables panel");
        require(state->releases == 1, "transfer failure releases resources immediately");
        require(state->frames.size() == static_cast<std::size_t>(failedFrame + 1),
                "short/failed frame is never retried or continued");
        board.show(canvas);
        require(state->frames.size() == static_cast<std::size_t>(failedFrame + 1),
                "disabled panel receives no later frames");
      }
      require(state->releases == 1, "failed board destructor does not release twice");
    }
  }
  auto state = std::make_shared<State>();
  state->acquire = false;
  {
    awtrix::Tc002Board board(fake(state));
    board.begin();
    require(!board.displayReady() && board.error() == "lock TC002 SPI: busy", "a refused panel lock propagates");
    require(state->frames.empty() && state->releases == 1, "partial acquire unwinds without frames");
  }
  require(state->releases == 1, "partial acquisition released only once");
  auto invalid = std::make_shared<State>();
  awtrix::Tc002Board board(fake(invalid));
  board.begin();
  awtrix::Canvas wrongSize(32, 8);
  board.show(wrongSize);
  require(!board.displayReady() && invalid->frames.size() == 2 && invalid->releases == 1,
          "invalid canvas refuses output and releases transport");
  awtrix::Tc002Board absent(nullptr);
  absent.begin();
  require(!absent.displayReady() && !absent.error().empty(), "missing transport fails safely");

  for (const auto failure : {-1, 0, 3071}) for (const int failedFrame : {2, 3}) {
    auto shutdown = std::make_shared<State>();
    shutdown->failTransfer = failedFrame;
    shutdown->failResult = failure;
    {
      awtrix::Tc002Board finalBoard(fake(shutdown));
      finalBoard.begin();
      require(finalBoard.displayReady(), "shutdown failure does not predate initialization");
    }
    require(shutdown->releases == 1 && shutdown->frames.size() == static_cast<std::size_t>(failedFrame + 1),
            "shutdown transfer failure still closes once without further frames");
  }
}

void optionalTimingScope() {
  using namespace awtrix;
  LinuxPanelTiming timing;
  timing.clock = []() -> int64_t { static int64_t now = 0; return ++now; };
  timing.active = true;
  auto measured = std::make_shared<State>();
  auto baseline = std::make_shared<State>();
  {
    Tc002Board enabled(fake(measured)), disabled(fake(baseline));
    enabled.setTiming(&timing); // Deliberately earlier than the production attachment.
    enabled.begin(); disabled.begin();
    require(measured->measuredTransfers == 0 && timing.shows == 0, "initialization is not measured");
    Canvas canvas(52, 16); canvas.clear(0xA02070);
    for (int i = 0; i < 3; ++i) { enabled.show(canvas); disabled.show(canvas); }
    require(measured->frames == baseline->frames, "optional metrics preserve every byte and transfer");
    require(timing.shows == 3 && timing.showFailures == 0 && measured->measuredTransfers == 3,
            "only runtime show transfers receive active measurement context");
    timing.active = false;
    enabled.show(canvas); disabled.show(canvas);
    require(timing.shows == 3 && measured->measuredTransfers == 3, "stopped window remains frozen");
    timing.active = true; // Simulate exception unwinding before report.stop().
  }
  require(measured->frames == baseline->frames && measured->measuredTransfers == 3 && timing.shows == 3,
          "destructor still blanks twice but never counts cleanup even while report is active");
  require(timing.phases[static_cast<std::size_t>(LinuxPanelPhase::Show)].count == 3 &&
          timing.phases[static_cast<std::size_t>(LinuxPanelPhase::Packing)].count == 3,
          "one packing and show span per measured runtime call");
  for (const auto result : {-1, 3071}) {
    auto failure = std::make_shared<State>();
    failure->failTransfer = 2; failure->failResult = result;
    LinuxPanelTiming failedTiming; failedTiming.clock = timing.clock; failedTiming.active = true;
    Tc002Board board(fake(failure)); board.setTiming(&failedTiming); board.begin();
    Canvas canvas(52, 16); board.show(canvas);
    require(!board.displayReady() && failedTiming.shows == 1 && failedTiming.showFailures == 1 &&
            failure->measuredTransfers == 1 && failure->releases == 1,
            "negative and short writes retain failure cleanup with accounting enabled");
    board.show(canvas);
    require(failedTiming.shows == 1, "failed board adds no phantom runtime samples");
  }
}

}

int main() {
  packingAndShutdown();
  continuousPipelineAndStillContent();
  brightnessAndGrade();
  failuresCloseWithoutMoreWrites();
  optionalTimingScope();
  std::puts("TC002 adapter contracts passed: RGB layout, protocol levels, brightness, cleanup and failures");
}
