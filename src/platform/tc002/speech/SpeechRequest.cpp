#include "platform/tc002/speech/SpeechRequest.h"


namespace awtrix::speech {

bool readText(const SpeechVoice& voice, std::string_view text, Plan& out, DispatchDetail& detail) {
  if (text.empty() || text.size() > kMaxTextBytes) {
    detail.message = "must be 1..512 bytes";
    return false;
  }
  if (!voice.prepare(text, out)) {
    detail.message = "no words to speak";
    return false;
  }
  return true;
}

}
