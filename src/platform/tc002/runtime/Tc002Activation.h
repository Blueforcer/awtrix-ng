#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace awtrix {

// Reports the clock's activation id (RuntimeContract.h kActivationFlag) to the AWTRIX Hub once,
// for the count of TC002 clocks and a planned licence with Ulanzi, and only after the user agreed in the web UI. The
// request carries the id and nothing else; the Hub answers 204 for a new id and a known one
// alike. The consent file keeps the user's answer, the marker file the id the Hub took, so later
// starts send nothing.
class Tc002Activation {
 public:
  static constexpr const char* kUrl = "https://awtrix.de/api/v1/activations";
  // After an attempt without an answer, the next one waits this long; the last delay repeats.
  static constexpr std::array<int64_t, 5> kRetryMs{60000, 300000, 1800000, 7200000, 21600000};

  // The status of a POST of body to kUrl, 0 without an answer. Runs on the activation's thread.
  using Post = std::function<int(const std::string& body)>;
  // Ends a post() still running, from the runtime's thread.
  using Interrupt = std::function<void()>;

  enum class Consent : uint8_t { Unasked, Given, Declined };
  enum class State : uint8_t { Pending, Sending, Done, Refused };

  struct Paths {
    std::string consent;
    std::string marker;
  };

  Tc002Activation(std::string id, Paths paths, Post post, Interrupt interrupt);
  Tc002Activation(const Tc002Activation&) = delete;
  Tc002Activation& operator=(const Tc002Activation&) = delete;
  ~Tc002Activation();

  Consent consent() const { return consent_; }
  // Keeps the answer; false when the consent file cannot take it, which leaves the answer as it was.
  // A report already sent stays sent; a declined one is not sent.
  bool setConsent(bool given);

  // online: connected, with the synchronized clock the certificate check needs. Starts an
  // attempt when consent is given, online and no retry delay runs, and takes the answer of one
  // that finished.
  void tick(int64_t nowMs, bool online);
  State state() const { return state_; }

 private:
  void finish(int64_t nowMs, int status);

  const std::string id_;
  const Paths paths_;
  Post post_;
  Interrupt interrupt_;
  Consent consent_ = Consent::Unasked;
  State state_ = State::Pending;
  int64_t nextAttemptMs_ = 0;
  std::size_t failures_ = 0;
  std::thread worker_;
  std::atomic<bool> answered_{false};
  std::atomic<int> status_{0};
};

}
