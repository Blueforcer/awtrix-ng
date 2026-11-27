#include "platform/host/app/win32/TiedHost.h"

#include <windows.h>

#include <utility>

namespace awtrix::hostapp::win32 {
namespace {

void CALLBACK signalled(PVOID context, BOOLEAN) { (*static_cast<std::function<void()>*>(context))(); }

}

TiedHost::TiedHost(std::function<void()> signal) : signal_(std::move(signal)) {}

TiedHost::~TiedHost() { stop(); }

bool TiedHost::start(const std::vector<std::string>& args) {
  unwatch();
  if (!process_.start(args)) return false;
  HANDLE wait = nullptr;
  if (!::RegisterWaitForSingleObject(&wait, process_.handle(), signalled, &signal_, INFINITE,
                                     WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD)) {
    process_.stop();
    return false;
  }
  wait_ = wait;
  return true;
}

void TiedHost::stop() {
  unwatch();
  process_.stop();
}

bool TiedHost::ended(int& exitCode) {
  const HANDLE handle = process_.handle();
  if (!handle || ::WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) return false;
  unwatch();
  return process_.ended(exitCode);
}

void TiedHost::unwatch() {
  if (wait_) ::UnregisterWaitEx(std::exchange(wait_, nullptr), INVALID_HANDLE_VALUE);
}

}
