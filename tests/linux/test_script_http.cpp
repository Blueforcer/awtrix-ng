#include "../support.h"
#include "../LogLines.h"
#include "RawServer.h"
#include "ThreadStacks.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/script/ScriptBindings.h"
#include "platform/linux/LinuxScriptHttp.h"
#include "platform/linux/host/vendor/httplib.h"
#include "system/Log.h"

using awtrix::net::RequestBody;
using awtrix::net::RequestFill;
using awtrix::net::RequestHeaders;
using awtrix::test::check;
using awtrix::test::require;
namespace script = awtrix::script;

namespace {

struct Server {
  httplib::Server http;
  std::thread listener;
  std::string origin;
  std::mutex mutex;
  std::vector<std::string> keys;

  Server() {
    http.Get("/key", [this](const httplib::Request& request, httplib::Response& response) {
      std::lock_guard<std::mutex> lock(mutex);
      keys.push_back(request.get_header_value("X-Key") + request.get_param_value("q"));
      response.set_content("ok", "text/plain");
    });
    http.Post("/body", [this](const httplib::Request& request, httplib::Response& response) {
      std::lock_guard<std::mutex> lock(mutex);
      keys.push_back(request.body);
      response.set_content("ok", "text/plain");
    });
    http.Get("/moved", [](const auto&, auto& response) { response.set_redirect("/key"); });
    const int port = http.bind_to_any_port("127.0.0.1");
    require(port > 0, "a loopback server");
    listener = std::thread([this] { http.listen_after_bind(); });
    http.wait_until_ready();
    origin = "http://127.0.0.1:" + std::to_string(port);
  }
  ~Server() {
    http.stop();
    listener.join();
  }
  std::vector<std::string> takeKeys() {
    std::vector<std::string> taken;
    std::lock_guard<std::mutex> lock(mutex);
    taken.swap(keys);
    return taken;
  }
};

struct Rig {
  Server server;
  std::mutex mutex;
  std::vector<std::string> calls;
  std::deque<script::HttpResult> results;
  awtrix::LinuxScriptHttp http{[this](const std::string& owner, std::string& url, RequestHeaders& headers,
                                      RequestBody& body) {
    std::lock_guard<std::mutex> lock(mutex);
    calls.push_back(owner + " " + url);
    const std::size_t at = url.find("{secret:k}");
    for (auto& header : headers) {
      if (header.second == "refuse") return RequestFill::Refused;
      if (header.second == "throw") throw std::runtime_error("fill");
      if (header.second != "{secret:k}") continue;
      header.second = "s3cret";
      return RequestFill::Filled;
    }
    const std::size_t inBody = body.head.find("{secret:k}");
    if (inBody != std::string::npos) {
      body.head.replace(inBody, 10, "s3cret");
      return RequestFill::Filled;
    }
    if (at == std::string::npos) return RequestFill::Unchanged;
    url.replace(at, 10, "s%26cret");
    return RequestFill::Filled;
  }};

  Rig() {
    http.begin([this](script::HttpResult result) {
      std::lock_guard<std::mutex> lock(mutex);
      results.push_back(std::move(result));
    });
  }

  script::HttpResult get(uint32_t id, const std::string& path, const std::string& key) {
    return fetch(id, server.origin + path, key);
  }

  bool offer(uint32_t id, const std::string& url, const std::string& key, const std::string& method = "GET",
             const std::string& body = "", uint32_t timeoutSec = 0) {
    script::HttpRequest request;
    request.id = id;
    request.method = method;
    request.url = url;
    request.body = body;
    request.headers.emplace_back("X-Key", key);
    request.maxBytes = 8192;
    request.timeoutSec = timeoutSec;
    script::BindingScope scope(nullptr, nullptr, "S");
    return http.request(request);
  }

  void ask(uint32_t id, const std::string& url, const std::string& key, const std::string& method = "GET",
           const std::string& body = "", uint32_t timeoutSec = 0) {
    require(offer(id, url, key, method, body, timeoutSec), "the request is queued");
  }

  void running(const std::string& url) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
      if (lastCall() == "S " + url) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(false, "the worker takes the request");
  }

  script::HttpResult next(int seconds = 5) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!results.empty()) {
          script::HttpResult result = std::move(results.front());
          results.pop_front();
          return result;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(false, "the request ends");
    return {};
  }

  script::HttpResult fetch(uint32_t id, const std::string& url, const std::string& key,
                           const std::string& method = "GET", const std::string& body = "") {
    ask(id, url, key, method, body);
    return next();
  }

  std::string lastCall() {
    std::lock_guard<std::mutex> lock(mutex);
    return calls.empty() ? std::string() : calls.back();
  }
};

void hostileAnswersEndWithAStatus() {
  const awtrix::test::ThreadStacks small(awtrix::test::kSmallStackBytes);
  Rig rig;
  uint32_t id = 100;
  for (const awtrix::test::HostileAnswer& hostile : awtrix::test::hostileAnswers()) {
    awtrix::test::RawServer server([&hostile](int peer) {
      awtrix::test::readRequest(peer);
      awtrix::test::sendText(peer, hostile.head);
    });
    const script::HttpResult result = rig.fetch(++id, server.url(), "plain");
    check(result.id == id && result.status == hostile.status,
          std::string(hostile.name) + " ends with " + std::to_string(result.status));
  }
}

awtrix::test::RawServer::Serve answerAfter(std::chrono::milliseconds delay) {
  return [delay](int peer) {
    awtrix::test::readRequest(peer);
    std::this_thread::sleep_for(delay);
    awtrix::test::sendText(peer, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nlate");
  };
}

void aTimeoutSetsTheWait() {
  Rig rig;
  script::HttpRequest request;
  request.id = 200;
  request.method = "GET";
  request.url = rig.server.origin + "/key";
  request.timeoutSec = 121;
  check(!rig.http.request(request), "more than two minutes is refused");

  awtrix::test::RawServer soon(answerAfter(std::chrono::milliseconds(1200)));
  rig.ask(201, soon.url(), "", "GET", "", 1);
  script::HttpResult result = rig.next();
  check(result.id == 201 && result.status == 0, "a shorter wait ends first: " + std::to_string(result.status));

  awtrix::test::RawServer late(answerAfter(std::chrono::seconds(6)));
  rig.ask(202, late.url(), "", "POST", "x", 8);
  result = rig.fetch(203, rig.server.origin + "/key", "plain");
  check(result.id == 203 && result.status == 200, "a long wait holds up no plain request: " + std::to_string(result.id));
  result = rig.next(10);
  check(result.id == 202 && result.status == 200 && result.body == "late",
        "a longer wait outlasts the 5 s of a plain request: " + std::to_string(result.status));
}

void aCancelEndsTheRequest() {
  Rig rig;
  awtrix::test::RawServer slow(answerAfter(std::chrono::seconds(4)));
  rig.ask(301, slow.url(), "", "POST", "x", 20);
  rig.running(slow.url());
  rig.ask(302, rig.server.origin + "/body", "", "POST", "queued", 20);
  rig.http.cancel(302);
  rig.http.cancel(301);
  script::HttpResult result = rig.next(2);
  check(result.id == 301 && result.status == 0, "a cancel ends the running request: " + std::to_string(result.id));
  rig.ask(303, rig.server.origin + "/key", "after", "GET", "", 5);
  result = rig.next();
  check(result.id == 303 && result.status == 200, "the next one goes on: " + std::to_string(result.id));
  check(rig.server.takeKeys() == std::vector<std::string>{"after"}, "and the cancelled one was never sent");
}

void aRequestThatWaitedTooLongIsNotSent() {
  Rig rig;
  awtrix::test::RawServer slow(answerAfter(std::chrono::milliseconds(script::kHttpTimeoutMs + 500)));
  rig.ask(311, slow.url(), "", "POST", "x", 40);
  rig.running(slow.url());
  rig.ask(312, rig.server.origin + "/body", "", "POST", "late", 40);
  script::HttpResult result = rig.next(40);
  check(result.id == 311 && result.status == 200, "the first one takes its time: " + std::to_string(result.id));
  result = rig.next(2);
  check(result.id == 312 && result.status == 0, "the one behind it fails at once: " + std::to_string(result.id));
  check(rig.server.takeKeys().empty(), "and is never sent");
}

void modbusKeepsItsQueue() {
  Rig rig;
  awtrix::test::RawServer slow(answerAfter(std::chrono::seconds(4)));
  rig.ask(321, slow.url(), "", "POST", "x", 20);
  rig.running(slow.url());
  rig.ask(322, "modbus://127.0.0.1:1/1/3/0/1", "", "GET", "", 20);
  const script::HttpResult result = rig.next(2);
  check(result.id == 322, "modbus does not wait behind a slow request: " + std::to_string(result.id));
}

void queuedBodiesHaveALimit() {
  constexpr std::size_t kLimit = 4 * 1024 * 1024;
  Rig rig;
  awtrix::test::RawServer slow(answerAfter(std::chrono::seconds(2)));
  rig.ask(331, slow.url(), "", "POST", "x", 20);
  rig.running(slow.url());
  rig.ask(332, rig.server.origin + "/body", "", "POST", std::string(kLimit, 'x'), 20);
  check(!rig.offer(333, rig.server.origin + "/body", "", "POST", "y", 20), "a byte more is refused");
  check(!rig.offer(334, rig.server.origin + "/body", "", "POST", "y"), "in either queue");
  rig.http.cancel(332);
  check(rig.offer(335, rig.server.origin + "/body", "", "POST", "y"), "a cancel gives the room back");
}

}

void aThreadOnlyForARequest() {
  const std::size_t before = awtrix::test::threadCount();
  std::atomic<int> answers{0};
  awtrix::LinuxScriptHttp http;
  script::HttpRequest request;
  request.id = 1;
  request.method = "GET";
  request.url = "http://127.0.0.1:9/";
  check(!http.request(request), "a request before begin is refused");
  http.begin([&](script::HttpResult) { ++answers; });
  check(awtrix::test::threadCount() == before, "begin runs no thread");
  request.url = "no url";
  check(!http.request(request) && awtrix::test::threadCount() == before, "a refused request starts none");
  request.url = "http://127.0.0.1:9/";
  check(http.request(request) && awtrix::test::threadCount() == before + 1, "the first request starts it");
  for (int i = 0; i < 500 && !answers; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  check(answers == 1, "and it answers");
  http.stop();
  check(awtrix::test::threadCount() == before, "stop ends it");
  check(!http.request(request), "and nothing is taken after it");
  {
    awtrix::LinuxScriptHttp unused;
    unused.begin([](script::HttpResult) {});
  }
  check(awtrix::test::threadCount() == before, "one that never worked ends too");
}

int main() {
  aThreadOnlyForARequest();
  Rig rig;
  script::HttpResult result = rig.get(1, "/key", "refuse");
  check(result.id == 1 && result.status == 0 && !result.ok && rig.server.takeKeys().empty(),
        "a refused fill ends with status 0 and sends nothing: " + std::to_string(result.status));
  check(rig.lastCall() == "S " + rig.server.origin + "/key",
        "the hook gets the app that asked, taken when it asked, and the URL: " + rig.lastCall());

  result = rig.get(2, "/key", "{secret:k}");
  check(result.status == 200 && result.ok && result.body == "ok" &&
            rig.server.takeKeys() == std::vector<std::string>{"s3cret"},
        "the server sees the filled value");

  result = rig.get(3, "/moved", "{secret:k}");
  check(result.status == 302 && rig.server.takeKeys().empty(),
        "a filled request follows no redirect: " + std::to_string(result.status));
  result = rig.get(4, "/moved", "plain");
  check(result.status == 200 && rig.server.takeKeys() == std::vector<std::string>{"plain"},
        "without a filled value a GET follows redirects as before: " + std::to_string(result.status));

  result = rig.get(5, "/key?q={secret:k}", "");
  check(result.status == 200 && rig.server.takeKeys() == std::vector<std::string>{"s&cret"},
        "the server sees the value in the query, sent from the filled URL");
  result = rig.get(6, "/moved?q={secret:k}", "");
  check(result.status == 302 && rig.server.takeKeys().empty(),
        "a value in the query follows no redirect either: " + std::to_string(result.status));

  awtrix::logbuf::setVerbose(true);
  const uint32_t mark = awtrix::test::logMark();
  result = rig.fetch(7, "http://127.0.0.1:1/x?q={secret:k}", "");
  const std::string log = awtrix::test::logAfter(mark);
  awtrix::logbuf::setVerbose(false);
  check(result.status == 0 && log.find("s%26cret") == std::string::npos && log.find("/x?q=") == std::string::npos,
        "a filled request that fails leaves its URL in no log: " + log);
  const std::string bodies = rig.server.origin + "/body";
  result = rig.fetch(8, bodies, "", "POST", "{\"k\":\"{secret:k}\"}");
  check(result.status == 200 && rig.server.takeKeys() == std::vector<std::string>{"{\"k\":\"s3cret\"}"},
        "the server sees the filled body");
  result = rig.fetch(9, bodies, "refuse", "POST", "{secret:k}");
  check(result.status == 0 && !result.ok && rig.server.takeKeys().empty(), "a refused fill sends no body");
  result = rig.fetch(10, bodies, "", "POST", "plain");
  check(result.status == 200 && rig.server.takeKeys() == std::vector<std::string>{"plain"},
        "a body without a placeholder goes out as it is");
  result = rig.fetch(11, rig.server.origin + "/moved", "", "GET", "{secret:k}");
  check(result.status == 302 && rig.server.takeKeys().empty(),
        "a filled body follows no redirect: " + std::to_string(result.status));
  awtrix::logbuf::setVerbose(true);
  const uint32_t bodyMark = awtrix::test::logMark();
  result = rig.fetch(12, "http://127.0.0.1:1/x", "", "POST", "marker {secret:k}");
  const std::string bodyLog = awtrix::test::logAfter(bodyMark);
  awtrix::logbuf::setVerbose(false);
  check(result.status == 0 && bodyLog.find("s3cret") == std::string::npos && bodyLog.find("marker") == std::string::npos,
        "a filled body that fails reaches no log: " + bodyLog);
  result = rig.get(90, "/key", "throw");
  check(result.id == 90 && result.status == 0 && !result.ok, "a fill that throws ends with status 0");
  result = rig.get(91, "/key", "plain");
  check(result.status == 200 && rig.server.takeKeys().back() == "plain", "and the worker goes on");
  rig.http.stop();
  hostileAnswersEndWithAStatus();
  aTimeoutSetsTheWait();
  aCancelEndsTheRequest();
  aRequestThatWaitedTooLongIsNotSent();
  modbusKeepsItsQueue();
  queuedBodiesHaveALimit();
  return awtrix::test::finish("linux-script-http");
}
