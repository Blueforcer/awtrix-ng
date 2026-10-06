#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <sys/types.h>

#include "platform/posix/Files.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

namespace awtrix::tc002d {
struct RuntimeLinks;

// Bounded microphone replies for the current runtime generation.
class MicrophoneRelay {
 public:
  // Borrowed fields owned by RuntimeChild; they outlive this relay.
  struct Peer {
    const posix::UniqueFd& channel;
    const bool& hello;
    const pid_t& pid;
    const unsigned& generation;
    unsigned& sendErrors;
    const std::shared_ptr<char>& alive;
  };
  MicrophoneRelay(RuntimeLinks& links, Peer peer) : links_(links), peer_(peer) {}
  MicrophoneRelay(const MicrophoneRelay&) = delete;
  MicrophoneRelay& operator=(const MicrophoneRelay&) = delete;
  void requestPcm(int id, int64_t nowMs);
  void requestStream(const tc002::StreamControl& control, int64_t nowMs);
  void flushPcm();
  void flushStream();
  void revokeStream();
  void discardPcmReply() { pcmReply_.clear(); }
  bool pcmQueued() const { return !pcmReply_.empty(); }
  bool queued() const { return pcmQueued() || !streamReplies_.empty(); }

 private:
  bool queueStream(const tc002::StreamEvent& event);
  RuntimeLinks& links_;
  Peer peer_;
  int streamEpoch_ = 0;
  std::deque<std::string> streamReplies_;
  // A completion stays until consumed and never reaches a later runtime generation.
  std::string pcmReply_;
  bool pcmPending_ = false;
  int64_t pcmRequestAt_ = -1;
};

}  // namespace awtrix::tc002d
