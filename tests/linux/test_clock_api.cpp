#include "../support.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/remote/ClockApi.h"

using awtrix::remote::ClockApi;
using awtrix::test::check;
using awtrix::test::require;

namespace {

constexpr const char* kKey = "0102030405060708091011121314151617181920212223242526272829303132";

template <class F>
bool until(F done) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return done();
}

struct Clock {
  httplib::Server server;
  std::thread listener;
  int port = 0;
  std::atomic<bool> blocked{false};
  std::atomic<bool> release{false};
  std::mutex mutex;
  std::vector<std::string> seen;

  explicit Clock(const char* name) {
    server.Get("/block", [this](const httplib::Request&, httplib::Response& res) {
      blocked = true;
      while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      res.set_content("released", "text/plain");
    });
    server.Get("/big", [](const httplib::Request&, httplib::Response& res) {
      res.set_content(std::string(64 * 1024 + 1, 'x'), "text/plain");
    });
    server.Post("/echo", [this, name](const httplib::Request& req, httplib::Response& res) {
      std::lock_guard<std::mutex> lock(mutex);
      seen.push_back(req.get_header_value("Authorization") + "|" + req.get_header_value("Content-Type") + "|" +
                     req.body);
      res.status = 422;
      res.set_content(name, "text/plain");
    });
    server.Get("/small", [name](const httplib::Request&, httplib::Response& res) { res.set_content(name, "text/plain"); });
    port = server.bind_to_any_port("127.0.0.1");
    require(port > 0, "bind a loopback clock");
    listener = std::thread([this] { server.listen_after_bind(); });
    require(until([this] { return server.is_running(); }), "the clock listens");
  }
  ~Clock() {
    release = true;
    server.stop();
    listener.join();
  }
  std::string origin() const { return "http://127.0.0.1:" + std::to_string(port); }
};

}

int main() {
  Clock first("first");
  Clock second("second");
  ClockApi api(first.origin(), kKey);
  std::map<int, int> statuses;
  std::vector<std::string> bodies;
  int answered = 0;
  const auto record = [&](int status, const std::string& body) {
    ++statuses[status];
    bodies.push_back(body);
    ++answered;
  };

  api.request("POST", "/echo", "{\"act\":\"next\"}", record);
  check(until([&] { api.poll(); return answered == 1; }), "an answer arrives through poll");
  check(statuses[422] == 1 && bodies.back() == "first", "a refusal comes back with its status and body");
  {
    std::lock_guard<std::mutex> lock(first.mutex);
    check(first.seen.size() == 1 && first.seen[0] == std::string("Bearer ") + kKey + "|application/json|{\"act\":\"next\"}",
          "the key goes along as Bearer token, the body as JSON");
  }

  api.request("GET", "/big", {}, record);
  check(until([&] { api.poll(); return answered == 2; }), "a too large answer is answered");
  check(statuses[0] == 1 && bodies.back().empty(), "an answer above 64 KB counts as none");

  api.request("GET", "/block", {}, record);
  require(until([&] { return first.blocked.load(); }), "the worker is busy with one request");
  for (int i = 0; i < 40; ++i) api.request("GET", "/small", {}, record);
  api.poll();
  check(answered == 2 + 8 && statuses[0] == 1 + 8, "beyond 32 waiting the oldest are dropped, each answered with 0");
  first.release = true;
  check(until([&] { api.poll(); return answered == 2 + 41; }), "every request is answered exactly once");
  check(statuses[200] == 1 + 32, "the waiting ones are served after the busy one");

  api.setOrigin(second.origin());
  api.request("GET", "/small", {}, record);
  check(until([&] { api.poll(); return answered == 2 + 42; }) && bodies.back() == "second",
        "requests queued after a new origin go there");

  ClockApi silent("http://127.0.0.1:1", kKey);
  int none = -1;
  silent.request("GET", "/small", {}, [&](int status, const std::string&) { none = status; });
  check(until([&] { silent.poll(); return none >= 0; }) && none == 0, "no answer is status 0");
  return awtrix::test::finish("linux-clock-api");
}
