#include "sim/SimTerminalMatrix.h"

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define ISATTY _isatty
#define FILENO _fileno
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <csignal>
#define ISATTY isatty
#define FILENO fileno
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/CoreEngine.h"
#include "core/render/Canvas.h"

namespace awtrix {

namespace {

constexpr int kTitleRow = 1;
constexpr int kFrameTopRow = 2;
constexpr int kPixelRow0 = 3;
// Rows spent on everything that is not panel pixels: title, border, status line and log headroom.
constexpr int kChromeRows = 11;
constexpr int kMaxScale = 16;

long long steadyMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int envSize(const char* name) {
  const char* v = std::getenv(name);
  if (!v || !*v) return 0;
  const long n = std::strtol(v, nullptr, 10);
  return (n > 0 && n < 10000) ? static_cast<int>(n) : 0;
}

int terminalRows() {
  if (const int n = envSize("LINES")) return n;
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return info.srWindow.Bottom - info.srWindow.Top + 1;
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
#endif
  return 30;
}

int terminalCols() {
  if (const int n = envSize("COLUMNS")) return n;
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    return info.srWindow.Right - info.srWindow.Left + 1;
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
#endif
  return 80;
}

// Drops the scroll region, shows the cursor again and parks it at the bottom. Registered with
// atexit and reached through the Ctrl+C handlers, otherwise a quit leaves the shell unusable.
void restoreTerminal() {
  std::fputs("\x1b[r\x1b[?25h\x1b[0m\x1b[9999;1H\n", stdout);
  std::fflush(stdout);
}

#if defined(_WIN32)
BOOL WINAPI ctrlHandler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT) {
    std::exit(0);
  }
  return FALSE;
}
#endif

// sgr is "38" for foreground or "48" for background. The frame arrives already dimmed - SimBoard
// folds brightness into the grade - so scaling here would dim the preview twice.
void appendColor(std::string& out, const char* sgr, uint32_t rgb) {
  const unsigned r = (rgb >> 16) & 0xFF;
  const unsigned g = (rgb >> 8) & 0xFF;
  const unsigned b = rgb & 0xFF;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "\x1b[%s;2;%u;%u;%um", sgr, r, g, b);
  out += buf;
}

}

bool SimTerminalMatrix::begin(Mode mode, uint16_t port, CoreEngine* engine) {
  if (mode == Mode::Off) return false;
  const bool tty = ISATTY(FILENO(stdout)) != 0;
  if (mode == Mode::Auto && !tty) return false;

#if defined(_WIN32)
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD conMode = 0;
  if (GetConsoleMode(h, &conMode))
    SetConsoleMode(h, conMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  else if (mode == Mode::Auto)
    return false;
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(ctrlHandler, TRUE);
#else
  std::signal(SIGINT, [](int) { std::exit(0); });
  std::signal(SIGTERM, [](int) { std::exit(0); });
#endif

  engine_ = engine;
  port_ = port;
  active_ = true;
  fpsWindowStartMs_ = steadyMs();
  layoutCheckMs_ = fpsWindowStartMs_;
  std::atexit(restoreTerminal);
  computeLayout();
  drawStatic();
  return true;
}

bool SimTerminalMatrix::computeLayout() {
  const int cols = terminalCols();
  const int rows = terminalRows();
  const int byWidth = (cols - 2) / panelW_;
  const int byHeight = (rows - kChromeRows) * 2 / panelH_;
  // Scale is forced even because a character cell always carries two LED rows via a half block, so
  // an odd scale would leave half a row dangling.
  int scale = std::min(byWidth, byHeight) & ~1;
  scale = std::max(1, std::min(scale, kMaxScale));

  const int pixelRows = (panelH_ * scale + 1) / 2;
  const int panelCol = std::max(1, 1 + (cols - (panelW_ * scale + 2)) / 2);
  const bool changed = scale != scale_ || cols != termCols_ || rows != termRows_ ||
                       pixelRows != pixelRows_ || panelCol != panelCol_;
  scale_ = scale;
  termCols_ = cols;
  termRows_ = rows;
  panelCol_ = panelCol;
  pixelRow0_ = kPixelRow0;
  pixelRows_ = pixelRows;
  statusRow_ = pixelRow0_ + pixelRows_ + 1;
  logTopRow_ = statusRow_ + 2;
  return changed;
}

void SimTerminalMatrix::drawStatic() {
  const int inner = panelW_ * scale_;
  const int rightCol = panelCol_ + 1 + inner;
  std::string out;
  out += "\x1b[2J\x1b[?25l";
  char buf[128];
  std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[1mAWTRIX NG Simulator\x1b[0m  http://localhost:%u",
                kTitleRow, panelCol_, static_cast<unsigned>(port_));
  out += buf;
  std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", kFrameTopRow, panelCol_);
  out += buf;
  out += "┌";
  for (int i = 0; i < inner; ++i) out += "─";
  out += "┐";
  for (int r = 0; r < pixelRows_; ++r) {
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH│\x1b[%d;%dH│", pixelRow0_ + r, panelCol_,
                  pixelRow0_ + r, rightCol);
    out += buf;
  }
  std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", pixelRow0_ + pixelRows_, panelCol_);
  out += buf;
  out += "└";
  for (int i = 0; i < inner; ++i) out += "─";
  out += "┘";
  // Fence the terminal's scroll region to the rows below the panel: log output then scrolls inside
  // it and can never push the matrix off the screen.
  std::snprintf(buf, sizeof(buf), "\x1b[%d;%dr\x1b[%d;1H", logTopRow_,
                std::max(logTopRow_, termRows_), logTopRow_);
  out += buf;
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

void SimTerminalMatrix::render(const Canvas& canvas, uint8_t brightness) {
  if (!active_) return;
  ++fpsCount_;
  const long long now = steadyMs();
  const bool statusDue = now - fpsWindowStartMs_ >= 1000;
  if (statusDue) {
    fpsShown_ = fpsCount_;
    fpsCount_ = 0;
    fpsWindowStartMs_ = now;
  }

  bool relaid = false;
  if (canvas.width() != panelW_ || canvas.height() != panelH_) {
    panelW_ = canvas.width();
    panelH_ = canvas.height();
    relaid = true;
  }
  if (relaid || now - layoutCheckMs_ >= 500) {
    layoutCheckMs_ = now;
    relaid = computeLayout() || relaid;
  }
  if (relaid) {
    drawStatic();
    lastFrame_.clear();
  }

  const int scale = scale_;
  const int cells = panelW_ * scale;

  std::string frame;
  frame.reserve(static_cast<size_t>(cells) * pixelRows_ * 6 + 512);
  char buf[64];
  // Two LEDs per cell: the upper half block is painted in the top pixel's colour and its background
  // shows through as the bottom one. Colours are only re-emitted when they actually change.
  if (scale == 1) {
    for (int r = 0; r < pixelRows_; ++r) {
      std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", pixelRow0_ + r, panelCol_ + 1);
      frame += buf;
      uint32_t lastFg = 0xFFFFFFFFu, lastBg = 0xFFFFFFFFu;
      for (int x = 0; x < panelW_; ++x) {
        const uint32_t top = canvas.getPixel(x, r * 2);
        const uint32_t bot = canvas.getPixel(x, r * 2 + 1);
        if (top != lastFg) appendColor(frame, "38", top);
        if (bot != lastBg) appendColor(frame, "48", bot);
        lastFg = top;
        lastBg = bot;
        frame += "▀";
      }
      frame += "\x1b[0m";
    }
  } else {
    // Zoomed in, each LED becomes a scale x scale block of cells. The last column and row of a
    // block use edge glyphs so a dark seam is left between LEDs, which is what sells the panel look.
    const int ledCellRows = scale / 2;
    for (int r = 0; r < pixelRows_; ++r) {
      std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", pixelRow0_ + r, panelCol_ + 1);
      frame += buf;
      appendColor(frame, "38", 0);
      const int y = r / ledCellRows;
      const bool lastRow = (r % ledCellRows) == ledCellRows - 1;
      uint32_t lastBg = 0xFFFFFFFFu;
      for (int cx = 0; cx < cells; ++cx) {
        const uint32_t c = canvas.getPixel(cx / scale, y);
        if (c == 0) {
          if (lastBg != 0) {
            appendColor(frame, "48", 0);
            lastBg = 0;
          }
          frame += ' ';
          continue;
        }
        if (c != lastBg) {
          appendColor(frame, "48", c);
          lastBg = c;
        }
        const bool lastCol = (cx % scale) == scale - 1;
        frame += lastCol ? (lastRow ? "▟" : "▐") : (lastRow ? "▄" : " ");
      }
      frame += "\x1b[0m";
    }
  }

  const bool frameChanged = frame != lastFrame_;
  if (!frameChanged && !statusDue && !relaid) return;

  // Save and restore the cursor around the write so log lines carry on scrolling from wherever they
  // left off in the region below.
  std::string out = "\x1b" "7";
  if (frameChanged) {
    out += frame;
    lastFrame_ = frame;
  }
  if (statusDue || relaid) {
    const char* app = engine_ ? engine_->currentAppId().c_str() : "";
    std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[0m%3u fps  bri %3u  app %.20s\x1b[K",
                  statusRow_, panelCol_, static_cast<unsigned>(fpsShown_),
                  static_cast<unsigned>(brightness), app);
    out += buf;
  }
  out += "\x1b" "8";
  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

}
