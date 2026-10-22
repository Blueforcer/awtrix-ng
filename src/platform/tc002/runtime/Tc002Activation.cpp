#include "platform/tc002/runtime/Tc002Activation.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include "platform/posix/Files.h"
#include "system/Log.h"

namespace awtrix {

namespace {

constexpr const char* kGiven = "yes";
constexpr const char* kDeclined = "no";

// No answer, a timeout, the rate limit or a server fault: the same request may succeed later.
bool transient(int status) { return status == 0 || status == 408 || status == 429 || status >= 500; }

std::string kept(const std::string& path) {
  std::string text;
  return posix::readText(path, text, 128) ? posix::trimmed(text) : std::string();
}

}

Tc002Activation::Tc002Activation(std::string id, Paths paths, Post post, Interrupt interrupt)
    : id_(std::move(id)), paths_(std::move(paths)), post_(std::move(post)), interrupt_(std::move(interrupt)) {
  const std::string answer = kept(paths_.consent);
  if (answer == kGiven) consent_ = Consent::Given;
  else if (answer == kDeclined) consent_ = Consent::Declined;
  if (kept(paths_.marker) == id_) state_ = State::Done;
}

Tc002Activation::~Tc002Activation() {
  if (!worker_.joinable()) return;
  if (interrupt_) interrupt_();
  worker_.join();
}

bool Tc002Activation::setConsent(bool given) {
  if (!posix::replaceText(paths_.consent, std::string(given ? kGiven : kDeclined) + "\n")) {
    logf("activation: answer not kept in %s: %s", paths_.consent.c_str(), std::strerror(errno));
    return false;
  }
  consent_ = given ? Consent::Given : Consent::Declined;
  logf("activation: %s in the web UI", given ? "agreed" : "declined");
  return true;
}

void Tc002Activation::tick(int64_t nowMs, bool online) {
  if (state_ == State::Sending) {
    if (!answered_.load(std::memory_order_acquire)) return;
    worker_.join();
    finish(nowMs, status_.load(std::memory_order_relaxed));
    return;
  }
  if (state_ != State::Pending || consent_ != Consent::Given || !online || nowMs < nextAttemptMs_) return;
  state_ = State::Sending;
  answered_.store(false, std::memory_order_relaxed);
  worker_ = std::thread([this, body = "{\"id\":\"" + id_ + "\"}"] {
    status_.store(post_(body), std::memory_order_relaxed);
    answered_.store(true, std::memory_order_release);
  });
}

void Tc002Activation::finish(int64_t nowMs, int status) {
  if (status >= 200 && status < 300) {
    state_ = State::Done;
    if (posix::replaceText(paths_.marker, id_ + "\n")) logf("activation: reported to the AWTRIX Hub");
    else logf("activation: reported to the AWTRIX Hub, not kept in %s: %s", paths_.marker.c_str(), std::strerror(errno));
    return;
  }
  if (!transient(status)) {
    state_ = State::Refused;
    logf("activation: the AWTRIX Hub refused it (HTTP %d); not sent again until the next start", status);
    return;
  }
  const int64_t delayMs = kRetryMs[failures_ < kRetryMs.size() ? failures_ : kRetryMs.size() - 1];
  ++failures_;
  state_ = State::Pending;
  nextAttemptMs_ = nowMs + delayMs;
  if (status == 0) logf("activation: no answer from the AWTRIX Hub; next try in %lld s", static_cast<long long>(delayMs / 1000));
  else logf("activation: the AWTRIX Hub answered HTTP %d; next try in %lld s", status, static_cast<long long>(delayMs / 1000));
}

}
