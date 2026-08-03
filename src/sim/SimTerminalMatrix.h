#pragma once

#include <cstdint>
#include <string>

namespace awtrix {

class Canvas;
class CoreEngine;

class SimTerminalMatrix {
 public:
  // Auto draws only when stdout is a real terminal, so piping the sim into a file stays clean.
  enum class Mode { Auto, On, Off };

  bool begin(Mode mode, uint16_t port, CoreEngine* engine);
  void render(const Canvas& canvas, uint8_t brightness);
  bool active() const { return active_; }

 private:
  bool computeLayout();
  void drawStatic();

  bool active_ = false;
  CoreEngine* engine_ = nullptr;
  uint16_t port_ = 0;
  std::string lastFrame_;
  uint16_t fpsCount_ = 0;
  uint16_t fpsShown_ = 0;
  long long fpsWindowStartMs_ = 0;
  long long layoutCheckMs_ = 0;

  // Rows and columns are 1-based throughout, because that is what ANSI cursor addressing uses.
  int panelW_ = 32;
  int panelH_ = 8;
  int scale_ = 1;
  int termCols_ = 0;
  int termRows_ = 0;
  int panelCol_ = 1;
  int pixelRow0_ = 3;
  int pixelRows_ = 4;
  int statusRow_ = 8;
  int logTopRow_ = 10;
};

}
