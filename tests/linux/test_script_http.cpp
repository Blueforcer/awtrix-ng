#include "../support.h"
#include "../LogLines.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/script/ScriptBindings.h"
#include "platform/linux/LinuxScriptHttp.h"
#include "platform/linux/host/vendor/httplib.h"
#include "system/Log.h"

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
  awtrix::LinuxScriptHttp http{[this](const std::string& owner, std::string& url, RequestHeaders& headers) {
    std::lock_guard<std::mutex> lock(mutex);
    calls.push_back(owner + " " + url);
    const std::size_t at = url.find("{secret:k}");
    for (auto& header : headers) {
      if (header.second == "refuse") return RequestFill::Refused;
      if (header.second != "{secret:k}") continue;
      header.second = "s3cret";
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

  script::HttpResult fetch(uint32_t id, const std::string& url, const std::string& key) {
    script::HttpRequest request;
    request.id = id;
    request.method = "GET";
    request.url = url;
    request.headers.emplace_back("X-Key", key);
    request.maxBytes = 8192;
    {
      script::BindingScope scope(nullptr, nullptr, "S");
      require(http.request(request), "the request is queued");
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
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

  std::string lastCall() {
    std::lock_guard<std::mutex> lock(mutex);
    return calls.empty() ? std::string() : calls.back();
  }
};

}

int main() {
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
  rig.http.stop();
  return awtrix::test::finish("linux-script-http");
}
