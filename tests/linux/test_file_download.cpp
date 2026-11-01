#include "../support.h"
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/net/FileDownload.h"
#include "platform/linux/net/HttpGet.h"
#include "platform/posix/Files.h"
using awtrix::net::FileDownload;
using Failure = FileDownload::Failure;
constexpr auto check = awtrix::test::require;
template <class F>
bool until(F done) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}
int main() {
  httplib::Server server;
  std::atomic<bool> entered{false}, release{false};
  server.Get("/small", [](const auto&, auto& r) { r.set_content("speech-bytes", "audio/wav"); });
  server.Get("/large", [](const auto&, auto& r) {
    r.set_content(std::string(64 * 1024 + 1, 'x'), "audio/wav");
  });
  server.Get("/chunked", [](const auto&, auto& r) {
    r.set_chunked_content_provider("audio/mpeg", [](std::size_t offset, httplib::DataSink& sink) {
      if (offset >= 48 * 1024) {
        sink.done();
        return true;
      }
      const std::string block(4096, 'y');
      return sink.write(block.data(), block.size());
    });
  });
  server.Get("/empty", [](const auto&, auto& r) { r.set_content("", "audio/mpeg"); });
  server.Post("/echo", [](const auto& request, auto& r) {
    r.status = request.get_header_value("X-Test") == "sent" ? 201 : 400;
    r.set_content(request.body, "text/plain");
  });
  server.Get("/agent", [](const auto& request, auto& r) {
    r.set_content(std::to_string(request.get_header_value_count("User-Agent")) + " " +
                      request.get_header_value("User-Agent"), "text/plain");
  });
  server.Get("/wire-limit", [](const auto&, auto& r) {
    r.set_content(std::string(1024 * 1024, 'z'), "text/plain");
  });
  server.Get("/wire-overflow", [](const auto&, auto& r) {
    r.set_content(std::string(1024 * 1024 + 1, 'z'), "text/plain");
  });
  server.Get("/missing", [](const auto&, auto& r) { r.status = 404; });
  server.Get("/nothing", [](const auto&, auto& r) { r.status = 204; });
  server.Get("/redirect", [](const auto&, auto& r) { r.set_redirect("/small"); });
  server.Get("/slow", [&](const auto&, auto& r) {
    entered = true;
    while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    r.set_content("old", "audio/wav");
  });
  int port = server.bind_to_any_port("127.0.0.1");
  check(port > 0, "bind loopback fixture");
  std::thread listener([&] { server.listen_after_bind(); });
  check(until([&] { return server.is_running(); }), "server starts");
  const std::string origin = "http://127.0.0.1:" + std::to_string(port);

  httplib::Client client(origin);
  awtrix::net::configure(client, {});
  std::string echoed;
  auto response = awtrix::net::send(client, "POST", "/echo", "request bytes", {{"X-Test", "sent"}},
                                  [&](const char* bytes, std::size_t count) {
    echoed.append(bytes, count);
    return true;
  });
  check(response && response->status == 201 && echoed == "request bytes",
        "any-method sender preserves headers, body and non-200 status");
  std::size_t received = 0;
  const auto receive = [&](const char*, std::size_t count) { received += count; return true; };
  response = awtrix::net::send(client, "GET", "/wire-limit", "", {}, receive);
  check(response && received == 1024 * 1024, "sender accepts exactly the one-MiB boundary");
  received = 0;
  response = awtrix::net::send(client, "GET", "/wire-overflow", "", {}, receive);
  check(!response && received <= 1024 * 1024, "sender never delivers bytes beyond its limit");
  response = awtrix::net::send(client, "GET", "/small", "", {},
                              [](const char*, std::size_t) { return false; });
  check(!response, "receiver refusal stops the sender");
  std::string agent;
  const auto collect = [&](const char* bytes, std::size_t count) { agent.append(bytes, count); return true; };
  response = awtrix::net::send(client, "GET", "/agent", "", {}, collect);
  check(response && agent == "1 AWTRIX-NG", "sender names the clock as User-Agent");
  agent.clear();
  response = awtrix::net::send(client, "GET", "/agent", "", {{"user-agent", "Script/1"}}, collect);
  check(response && agent == "1 Script/1", "a caller's User-Agent replaces the default");
  awtrix::net::ResponseState state;
  const awtrix::net::BodyPolicy upTo12{12, {}, {}}, upTo11{11, {}, {}};
  received = 0;
  response = awtrix::net::exchange(client, "GET", "/small", "", {}, upTo12, state, receive);
  check(response && state.status == 200 && state.contentType == "audio/wav" && state.announcedBytes == 12 &&
            state.receivedBytes == 12 && !state.tooLarge && received == 12,
        "exchange reports status, type and sizes and accepts its exact limit");
  received = 0;
  response = awtrix::net::exchange(client, "GET", "/small", "", {}, upTo11, state, receive);
  check(!response && state.tooLarge && state.status == 200 && received == 0,
        "an announced size above the limit stops before the body");
  received = 0;
  response = awtrix::net::exchange(client, "GET", "/chunked", "", {}, {8192, {}, {}}, state, receive);
  check(!response && state.tooLarge && received <= 8192, "a chunked body stops at the limit");
  echoed.clear();
  response = awtrix::net::exchange(client, "POST", "/echo", "x", {{"X-Test", "sent"}}, {64, {}, {}}, state,
                                   [&](const char* bytes, std::size_t count) {
    echoed.append(bytes, count);
    return true;
  });
  check(response && state.status == 201 && state.contentType.rfind("text/plain", 0) == 0 && echoed == "x",
        "exchange sends any method with headers and body");
  const std::string mappedPath = "/tmp/file-download-body-" + std::to_string(::getpid());
  check(awtrix::posix::replaceText(mappedPath, std::string(100 * 1024, 'm')), "a file for the body");
  const auto mapped = std::make_shared<const awtrix::posix::MappedFile>(mappedPath, 100 * 1024);
  ::unlink(mappedPath.c_str());
  echoed.clear();
  response = awtrix::net::exchange(client, "POST", "/echo", awtrix::net::RequestBody("head-", mapped, "-tail"),
                                   {{"X-Test", "sent"}}, {256 * 1024, {}, {}}, state,
                                   [&](const char* bytes, std::size_t count) {
    echoed.append(bytes, count);
    return true;
  });
  check(response && state.status == 201 && echoed == "head-" + std::string(100 * 1024, 'm') + "-tail",
        "a body around a mapped file goes out streamed, in order");
  received = 0;
  std::string judged;
  const awtrix::net::BodyPolicy htmlOnly{64, [&](int status, const std::string& type) {
    judged = std::to_string(status) + " " + type;
    return type == "text/html";
  }, {}};
  response = awtrix::net::exchange(client, "GET", "/missing", "", {}, htmlOnly, state, receive);
  check(!response && judged.rfind("404 ", 0) == 0 && state.status == 404 && !state.tooLarge,
        "the accept hook sees the status before the body");
  response = awtrix::net::exchange(client, "GET", "/small", "", {}, htmlOnly, state, receive);
  check(!response && judged == "200 audio/wav" && state.status == 200 && received == 0 && !state.tooLarge,
        "and a refused type stops the exchange before the body");
  std::size_t roomAsked = 0;
  const awtrix::net::BodyPolicy noRoom{64, {}, [&](std::size_t more) {
    roomAsked += more;
    return false;
  }};
  response = awtrix::net::exchange(client, "GET", "/small", "", {}, noRoom, state, receive);
  check(!response && roomAsked == 12 && received == 0 && !state.tooLarge,
        "the room check sees the announced size before the body");
  response = awtrix::net::exchange(client, "HEAD", "/small", "", {}, {64, {}, {}}, state, receive);
  check(response && state.status == 200 && state.contentType == "audio/wav" && state.receivedBytes == 0,
        "a HEAD answer still names its status and type");
  response = awtrix::net::exchange(client, "GET", "/nothing", "", {}, {64, {}, {}}, state, receive);
  check(response && state.status == 204, "a 204 answer still names its status");
  awtrix::net::FilteredAnswer answer = awtrix::net::sendFiltered(
      client, "POST", "/echo", "{\"a\":1,\"text\":\"hi\"}", {{"X-Test", "sent"}}, {"\"text\":", 11, 8192});
  check(answer.status == 201 && answer.ok && answer.body == "\"text\":\"hi\"",
        "the filtered sender keeps the window after the needle: " + answer.body);
  answer = awtrix::net::sendFiltered(client, "POST", "/echo", "plain", {{"X-Test", "sent"}}, {"\"text\":", 11, 8192});
  check(answer.status == 201 && !answer.ok && answer.body.empty(), "an answer without the needle keeps nothing");
  answer = awtrix::net::sendFiltered(client, "GET", "/wire-overflow", "", {}, {"", 0, 8192});
  check(answer.status == 0 && !answer.ok, "an answer past the wire limit is a network failure");

  awtrix::net::HttpGet get;
  std::string bytes;
  check(get.fetch(origin + "/agent", {64, 15000, false}, bytes).failure ==
            awtrix::net::HttpGet::Failure::None && bytes == "1 AWTRIX-NG",
        "bounded fetch names the clock as User-Agent");
  check(get.fetch(origin + "/small", {12, 15000, false}, bytes).failure ==
            awtrix::net::HttpGet::Failure::None && bytes == "speech-bytes",
        "bounded memory receiver accepts its exact boundary");
  check(get.fetch(origin + "/small", {11, 15000, false}, bytes).failure ==
            awtrix::net::HttpGet::Failure::TooLarge && bytes.empty(),
        "announced overflow clears partial memory output");
  check(get.fetch(origin + "/chunked", {20 * 1024, 15000, false}, bytes).failure ==
            awtrix::net::HttpGet::Failure::TooLarge && bytes.empty(),
        "chunked overflow clears partial memory output");

  FileDownload download("/tmp", "awtrix-voice", {64 * 1024, 15000, false, nullptr});
  FileDownload::Result result;
  auto generation = download.start(origin, "/small");
  check(until([&] { return download.poll(result); }) && result.success() &&
            result.generation == generation,
        "successful download");
  std::string content;
  struct stat st {};
  check(awtrix::posix::readText(result.path, content) && content == "speech-bytes",
        "response bytes exact");
  check(::stat(result.path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600,
        "temporary response private");
  check(result.path.rfind("/tmp/awtrix-voice-", 0) == 0, "response does not consume data flash");
  ::unlink(result.path.c_str());
  download.start(origin, "/large");
  check(until([&] { return download.poll(result); }) && result.failure == Failure::TooLarge &&
            result.path.empty(),
        "oversize response rejected and removed");
  download.start(origin, "/chunked");
  check(until([&] { return download.poll(result); }) && result.success(),
        "a body without a length within the limit arrives");
  ::unlink(result.path.c_str());
  download.start(origin, "/redirect");
  check(until([&] { return download.poll(result); }) && result.failure == Failure::Status &&
            result.status == 302,
        "redirect not followed");
  download.start(origin, "/missing");
  check(until([&] { return download.poll(result); }) && result.failure == Failure::Status &&
            result.status == 404,
        "an error status names itself");
  download.start(origin, "/empty");
  check(until([&] { return download.poll(result); }) && result.failure == Failure::Empty,
        "an empty body is no file");
  download.start(origin, "/slow");
  check(until([&] { return entered.load(); }), "slow response entered");
  const auto start = std::chrono::steady_clock::now();
  download.cancel();
  check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50),
        "cancel does not wait for network");
  release = true;
  generation = download.start(origin, "/small");
  check(until([&] { return download.poll(result); }) && result.success() &&
            result.generation == generation,
        "new request cannot receive old completion");
  ::unlink(result.path.c_str());

  std::atomic<std::size_t> asked{0};
  FileDownload followed("/tmp", "awtrix-test", {40 * 1024, 15000, true, [&](std::size_t more) {
                          asked += more;
                          return asked <= 20 * 1024;
                        }});
  followed.start(origin, "/redirect");
  asked = 0;
  check(until([&] { return followed.poll(result); }) && result.success(), "redirect followed");
  check(awtrix::posix::readText(result.path, content) && content == "speech-bytes",
        "redirect target arrives");
  ::unlink(result.path.c_str());
  followed.start(origin, "/large");
  check(until([&] { return followed.poll(result); }) && result.failure == Failure::TooLarge,
        "an announced size over the limit is refused");
  asked = 0;
  followed.start(origin, "/chunked");
  check(until([&] { return followed.poll(result); }) && result.failure == Failure::NoRoom &&
            result.path.empty() && asked > 20 * 1024,
        "a download that outgrows the room stops and leaves nothing");

  server.stop();
  listener.join();
  std::puts(
      "file download: private temporary files, limits, room, redirects, status, cancellation "
      "and generations passed");
}
