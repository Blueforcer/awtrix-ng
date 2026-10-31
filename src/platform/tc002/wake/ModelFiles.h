#pragma once

#include <string>

namespace awtrix::tc002::wake {

// A microWakeWord model and its settings from the .json file of the same name beside it: the
// trainer's custom.json (probability_cutoff and sliding_window_size at the top) or the ESPHome model
// manifest (the same keys under "micro").
struct ModelFiles {
  std::string model;
  std::string settingsPath;
  double cutoff = 0;
  long long window = 0;
};

// False and a reason when either file cannot be read, the settings are missing, or a feature_step_size
// beside them is not the frontend's 10 ms.
bool readModelFiles(const std::string& modelPath, ModelFiles& out, std::string& error);

}
