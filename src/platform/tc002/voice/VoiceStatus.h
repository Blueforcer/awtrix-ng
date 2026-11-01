#pragma once

namespace awtrix::tc002::voice {

enum VoiceStatus : int { kOk = 200, kSilence = -1, kBusy = -2, kCancelled = -3, kFull = -4, kGone = -5, kBad = -6 };

}
