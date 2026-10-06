#include "platform/linux/host/HostResolver.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

using awtrix::net::ResolveState;
using namespace std::chrono_literals;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #expression); } while (0)

void pollLocalhost(awtrix::net::IHostResolver& resolver) {
  const auto until = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < until) {
    const auto state = resolver.resolve("localhost");
    CHECK(state != ResolveState::Failed);
    if (state == ResolveState::Ready) {
      CHECK((resolver.address() >> 24) == 127);
      return;
    }
    std::this_thread::sleep_for(10ms);
  }
  throw std::runtime_error("localhost resolution timed out");
}

int main() {
  try {
    auto resolver = awtrix::net::makeSlowHostResolver(150);
    CHECK(resolver->resolve("localhost") == ResolveState::Pending);
    CHECK(resolver->resolve("127.0.0.42") == ResolveState::Ready);
    CHECK(resolver->address() == 0x7f00002a);
    std::this_thread::sleep_for(250ms);
    CHECK(resolver->address() == 0x7f00002a);
    // A literal must not poison the previous hostname's cache entry.
    CHECK(resolver->resolve("localhost") == ResolveState::Pending);
    pollLocalhost(*resolver);
    CHECK(resolver->resolve("localhost") == ResolveState::Ready);
    resolver->forget();
    CHECK(resolver->resolve("localhost") == ResolveState::Pending);
    resolver->forget();
    CHECK(resolver->resolve("") == ResolveState::Failed);
    CHECK(resolver->resolve(std::string("localhost\0bad", 13)) == ResolveState::Failed);

    // Destroy objects while real DNS workers are delayed; ASan checks pending-worker lifetime.
    std::vector<std::unique_ptr<awtrix::net::IHostResolver>> retired;
    for (int index = 0; index < 16; ++index) {
      auto pending = awtrix::net::makeSlowHostResolver(150);
      CHECK(pending->resolve("localhost") == ResolveState::Pending);
      retired.push_back(std::move(pending));
    }
    const auto before = std::chrono::steady_clock::now();
    retired.clear();
    CHECK(std::chrono::steady_clock::now() - before < 100ms);
    std::this_thread::sleep_for(350ms);
    std::puts("host resolver: literal/cache, forget, invalid input and pending destruction passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
