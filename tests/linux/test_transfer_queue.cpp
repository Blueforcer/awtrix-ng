#include "../support.h"
#include "../LogLines.h"

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/net/Multipart.h"
#include "platform/linux/net/TransferQueue.h"
#include "platform/posix/Files.h"
#include "system/Log.h"

using awtrix::net::RequestFill;
using awtrix::net::RequestBody;
using awtrix::net::RequestHeaders;
using awtrix::net::TransferDownload;
using awtrix::net::TransferQueue;
using awtrix::net::TransferRequest;
using awtrix::net::TransferResult;
using awtrix::test::check;
using awtrix::test::require;

namespace {

const std::string kRiff = "RIFF" + std::string(40, 'w');

std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool exists(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0;
}

template <class Done>
bool until(Done done) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < end) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

std::ptrdiff_t threads() {
  return std::distance(std::filesystem::directory_iterator("/proc/self/task"), std::filesystem::directory_iterator{});
}

bool wait(TransferQueue& queue, TransferResult& result) {
  return until([&] { return queue.poll(result); });
}

struct Server {
  httplib::Server http;
  std::thread listener;
  std::string origin;
  std::atomic<bool> release{false}, entered{false};
  std::mutex keyMutex;
  std::vector<std::string> keys;

  Server() {
    http.Get("/key", [this](const httplib::Request& request, httplib::Response& response) {
      std::lock_guard<std::mutex> lock(keyMutex);
      keys.push_back(request.get_header_value("X-Key") + request.get_param_value("q"));
      response.set_content(kRiff, "audio/wav");
    });
    http.Get("/moved-key", [](const auto&, auto& response) { response.set_redirect("/key"); });
    http.Post("/form", [](const httplib::Request& request, httplib::Response& response) {
      const bool form = request.has_file("file") && request.get_file_value("file").filename == "a.wav" &&
                        request.get_file_value("model").content == "whisper-1";
      response.set_content(std::string(form ? "{\"x\":1,\"text\":\"hello\"}" : "{\"error\":1}"), "application/json");
    });
    http.Post("/raw", [](const httplib::Request& request, httplib::Response& response) {
      response.status = request.get_header_value("Content-Type") == "audio/wav" ? 200 : 415;
      response.set_content(std::to_string(request.body.size()), "text/plain");
    });
    http.Get("/riff", [](const auto&, auto& response) { response.set_content(kRiff, "audio/wav"); });
    http.Get("/octet", [](const auto&, auto& response) { response.set_content(kRiff, "application/octet-stream"); });
    http.Get("/html", [](const auto&, auto& response) { response.set_content(kRiff, "text/html"); });
    http.Get("/junk", [](const auto&, auto& response) { response.set_content("not riff", "audio/mpeg"); });
    http.Get("/big", [](const auto&, auto& response) {
      response.set_content(std::string(64 * 1024 + 1, 'x'), "audio/mpeg");
    });
    http.Get("/missing", [](const auto&, auto& response) { response.status = 404; });
    http.Get("/moved", [](const auto&, auto& response) { response.set_redirect("/riff"); });
    http.Get("/slow", [this](const auto&, auto& response) {
      entered = true;
      while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      response.set_content(kRiff, "audio/wav");
    });
    http.Get("/after", [](const httplib::Request& request, httplib::Response& response) {
      std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(request.get_param_value("ms"))));
      response.set_content("late", "text/plain");
    });
    const int port = http.bind_to_any_port("127.0.0.1");
    require(port > 0, "a loopback server");
    listener = std::thread([this] { http.listen_after_bind(); });
    http.wait_until_ready();
    origin = "http://127.0.0.1:" + std::to_string(port);
  }
  ~Server() {
    release = true;
    http.stop();
    listener.join();
  }
  void hold() {
    entered = false;
    release = false;
  }
  std::vector<std::string> takeKeys() {
    std::vector<std::string> taken;
    std::lock_guard<std::mutex> lock(keyMutex);
    taken.swap(keys);
    return taken;
  }
};

struct Files {
  const std::string path = "/tmp/transfer-queue-test-" + std::to_string(::getpid());
  const std::string second = path + "-b";
  ~Files() {
    ::unlink(path.c_str());
    ::unlink(second.c_str());
  }
};

TransferRequest request(const std::string& origin, const std::string& target, const std::string& owner, int id) {
  TransferRequest r;
  r.owner = owner;
  r.id = id;
  r.method = "GET";
  r.origin = origin;
  r.target = target;
  r.timeoutMs = 15000;
  return r;
}

TransferRequest upload(const std::string& origin, const std::string& target, RequestBody body, std::string type) {
  TransferRequest r = request(origin, target, "A", 1);
  r.method = "POST";
  r.body = std::move(body);
  r.headers.emplace_back("Content-Type", std::move(type));
  r.filter.cap = 8192;
  return r;
}

TransferDownload riffInto(const std::string& path) {
  return {path, 64 * 1024,
          [](const std::string& type) { return type.rfind("audio/", 0) == 0 || type == "application/octet-stream"; },
          [](const std::string& file, std::string& type) {
            if (readAll(file).compare(0, 4, "RIFF") != 0) return false;
            type = "audio/wav";
            return true;
          },
          {}};
}

std::shared_ptr<const awtrix::posix::MappedFile> mapped(const std::string& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary) << bytes;
  auto file = std::make_shared<const awtrix::posix::MappedFile>(path, bytes.size());
  ::unlink(path.c_str());
  return file;
}

TransferRequest download(const std::string& origin, const std::string& target, const std::string& path,
                         const std::string& owner = "A", int id = 2) {
  TransferRequest r = request(origin, target, owner, id);
  r.download = riffInto(path);
  return r;
}

void destructionEndsAStalledDownload(Server& server) {
  const Files files;
  server.hold();
  const auto begun = std::chrono::steady_clock::now();
  {
    TransferQueue queue(2);
    require(queue.start(download(server.origin, "/slow", files.path)), "a download that stalls");
    require(until([&] { return server.entered.load(); }), "the stalled download reaches the server");
  }
  check(std::chrono::steady_clock::now() - begun < std::chrono::seconds(3),
        "the destructor ends a stalled download at once");
  server.release = true;
}

void theWorkerStartsWithTheFirstTransfer(Server& server) {
  const Files files;
  const std::ptrdiff_t before = threads();
  TransferQueue queue(2);
  check(threads() == before, "a queue without transfers runs no thread");
  TransferResult result;
  require(queue.start(download(server.origin, "/riff", files.path)) && wait(queue, result), "a first transfer");
  check(threads() == before + 1, "the first transfer starts the worker");
}

void unpolledResultsLoseTheirFiles(Server& server) {
  const Files files;
  std::atomic<bool> second{false};
  {
    TransferQueue queue(2);
    TransferRequest last = download(server.origin, "/riff", files.second, "A", 4);
    last.download->verify = [&second](const std::string&, std::string& type) {
      type = "audio/wav";
      second = true;
      return true;
    };
    require(queue.start(download(server.origin, "/riff", files.path, "A", 3)) && queue.start(last), "two downloads");
    require(until([&] { return second.load(); }), "the second one arrives, so the first one's result waits");
    check(exists(files.path), "the first one's file is there");
  }
  check(!exists(files.path), "a queue that goes away deletes the files of results never polled");
}

void uploads(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  const auto clip = mapped(files.path, kRiff);
  require(clip->valid(), "a mapped clip");
  const auto form =
      awtrix::net::buildMultipart({{"model", "whisper-1"}}, {"file", "a.wav", "audio/wav", clip->bytes()});
  require(form.has_value(), "a form");
  TransferRequest posted = upload(server.origin, "/form", RequestBody(form->head, clip, form->tail), form->contentType);
  posted.filter = {"\"text\":", 14, 8192};
  require(queue.start(posted), "an upload starts");
  require(wait(queue, result), "the upload ends");
  check(result.owner == "A" && result.id == 1 && result.status == 200 && result.ok &&
            result.body == "\"text\":\"hello\"" && result.file.empty(),
        "the server reads the form streamed from the file, and find/keep cut the answer: " + result.body);

  require(queue.start(upload(server.origin, "/raw", RequestBody({}, clip, {}), "audio/wav")), "a raw upload starts");
  require(wait(queue, result), "the raw upload ends");
  check(result.status == 200 && result.ok && result.body == std::to_string(kRiff.size()),
        "the file goes out as it is: " + std::to_string(result.status) + " " + result.body);
  require(queue.start(upload(server.origin, "/raw", kRiff, "audio/wav")) && wait(queue, result), "a body in memory");
  check(result.ok && result.body == std::to_string(kRiff.size()), "a body in memory goes out as it is: " + result.body);
}

void theBudgetCoversTheFirstByte(Server& server) {
  TransferQueue queue(2);
  TransferResult result;
  TransferRequest late = request(server.origin, "/after?ms=1200", "A", 30);
  late.filter.cap = 8192;
  late.timeoutMs = 1500;
  require(queue.start(late) && wait(queue, result), "an answer inside the budget");
  check(result.status == 200 && result.ok && result.body == "late",
        "a budget of 1.5 s waits longer than one whole second for the first byte: " + std::to_string(result.status));
  late.timeoutMs = 300;
  require(queue.start(late) && wait(queue, result), "an answer after the budget");
  check(result.status == 0 && !result.ok, "a budget that runs out ends with status 0: " + std::to_string(result.status));
}

void downloads(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  require(queue.start(download(server.origin, "/riff", files.path)), "a download starts");
  require(wait(queue, result), "the download ends");
  check(result.id == 2 && result.ok && result.status == 200 && result.file == files.path &&
            result.contentType == "audio/wav" && result.fileBytes == kRiff.size() && readAll(files.path) == kRiff,
        "the body lands in the file: " + result.contentType + " " + std::to_string(result.fileBytes));
  require(queue.start(download(server.origin, "/octet", files.path)) && wait(queue, result), "octet-stream");
  check(result.ok && result.contentType == "audio/wav", "the verify hook names the type the bytes have");
  for (const char* refused : {"/html", "/junk", "/big"}) {
    require(queue.start(download(server.origin, refused, files.path)) && wait(queue, result), refused);
    check(!result.ok && result.status == 200 && result.file.empty(), std::string("refused: ") + refused);
  }
  require(queue.start(download(server.origin, "/missing", files.path)) && wait(queue, result), "a missing file");
  check(!result.ok && result.status == 404, "a download keeps its error status: " + std::to_string(result.status));
  require(queue.start(download("http://127.0.0.1:1", "/x", files.path)) && wait(queue, result), "no server");
  check(!result.ok && result.status == 0, "a network failure has status 0: " + std::to_string(result.status));
}

void aDownloadStopsWhenItsBytesNoLongerFit(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  constexpr std::size_t kBig = 64 * 1024 + 1, kRoomForBytes = 32 * 1024;
  std::size_t asked = 0;
  TransferRequest cramped = download(server.origin, "/big", files.path);
  cramped.download->maxBytes = 2 * kBig;
  cramped.download->room = [&asked](std::size_t more) {
    asked += more;
    return asked <= kBig + kRoomForBytes;
  };
  require(queue.start(cramped) && wait(queue, result), "a download that runs out of room");
  check(!result.ok && result.noRoom && result.status == 200 && result.file.empty() && !exists(files.path),
        "a download stops once its bytes no longer fit, and its file goes");
  check(asked > kBig + kRoomForBytes,
        "the room is asked for the announced size, then for each block that arrives: " + std::to_string(asked));
}

void redirects(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  TransferRequest followed = download(server.origin, "/moved", files.path);
  followed.followRedirects = true;
  require(queue.start(followed) && wait(queue, result), "a redirect to follow");
  check(result.ok && result.status == 200, "a request that follows redirects arrives: " + std::to_string(result.status));
  require(queue.start(download(server.origin, "/moved", files.path)) && wait(queue, result), "a redirect to keep");
  check(!result.ok && result.status == 302, "one that does not reports the redirect: " + std::to_string(result.status));
}

TransferRequest keyed(const Server& server, const std::string& target, const std::string& key, int id) {
  TransferRequest r = request(server.origin, target, "A", id);
  r.headers.emplace_back("X-Key", key);
  r.followRedirects = true;
  r.filter.cap = 8192;
  return r;
}

void theWorkerFillsHeadersRightBeforeSending(Server& server) {
  const Files files;
  std::vector<std::string> calls;
  TransferQueue queue(2, [&calls](const std::string& owner, std::string& url, RequestHeaders& headers) {
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
  });
  TransferResult result;
  server.takeKeys();

  require(queue.start(keyed(server, "/key", "refuse", 20)) && wait(queue, result), "a refused request");
  check(result.id == 20 && result.status == 0 && !result.ok && server.takeKeys().empty(),
        "a refused fill ends with status 0 and sends nothing: " + std::to_string(result.status));
  TransferRequest refusedDownload = download(server.origin, "/key", files.path, "A", 21);
  refusedDownload.headers.emplace_back("X-Key", "refuse");
  require(queue.start(refusedDownload) && wait(queue, result), "a refused download");
  check(result.status == 0 && !result.ok && !exists(files.path) && server.takeKeys().empty(),
        "a refused download opens no file and sends nothing");

  require(queue.start(keyed(server, "/key", "{secret:k}", 22)) && wait(queue, result), "a filled request");
  check(result.status == 200 && server.takeKeys() == std::vector<std::string>{"s3cret"},
        "the server sees the filled value");
  check(calls.size() == 3 && calls.back() == "A " + server.origin + "/key", "the hook gets the owner and the URL");

  require(queue.start(keyed(server, "/moved-key", "{secret:k}", 23)) && wait(queue, result), "a filled redirect");
  check(result.status == 302 && server.takeKeys().empty(),
        "a filled request follows no redirect: " + std::to_string(result.status));
  require(queue.start(keyed(server, "/moved-key", "plain", 24)) && wait(queue, result), "an unfilled redirect");
  check(result.status == 200 && server.takeKeys() == std::vector<std::string>{"plain"},
        "without a filled value redirects are followed as before: " + std::to_string(result.status));

  require(queue.start(keyed(server, "/key?q={secret:k}", "", 25)) && wait(queue, result), "a filled query");
  check(result.status == 200 && server.takeKeys() == std::vector<std::string>{"s&cret"},
        "the server sees the value in the query, sent from the filled URL");
  check(calls.back() == "A " + server.origin + "/key?q={secret:k}", "the hook gets the URL with its placeholder");
  require(queue.start(keyed(server, "/moved-key?q={secret:k}", "", 26)) && wait(queue, result),
          "a filled query on a redirect");
  check(result.status == 302 && server.takeKeys().empty(),
        "a value in the query follows no redirect either: " + std::to_string(result.status));

  awtrix::logbuf::setVerbose(true);
  const uint32_t mark = awtrix::test::logMark();
  TransferRequest unreachable = keyed(server, "/x?q={secret:k}", "", 27);
  unreachable.origin = "http://127.0.0.1:1";
  require(queue.start(unreachable) && wait(queue, result), "a filled request nobody answers");
  const std::string log = awtrix::test::logAfter(mark);
  awtrix::logbuf::setVerbose(false);
  check(result.status == 0 && log.find("s%26cret") == std::string::npos && log.find("/x?q=") == std::string::npos,
        "a filled request that fails leaves its URL in no log: " + log);
}

void cancelTakesBackAnUnpolledResult(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  server.hold();
  require(queue.start(download(server.origin, "/riff", files.path, "A", 5)) &&
              queue.start(download(server.origin, "/slow", files.second, "B", 6)),
          "A's download queued before B's slow one");
  require(until([&] { return server.entered.load(); }), "B's download reaches the server, so A's has ended");
  const auto taken = queue.cancel("A");
  check(taken.size() == 1 && taken[0] == 5 && !exists(files.path),
        "cancel takes back a result not yet polled and deletes its file: " + std::to_string(taken.size()));
  server.release = true;
  require(wait(queue, result), "B's download ends");
  check(result.owner == "B" && result.id == 6 && result.ok, "only B's result arrives: " + result.owner);
}

void cancelCatchesARunningCheck(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  std::atomic<bool> verifying{false}, cancelled{false};
  TransferRequest held = download(server.origin, "/riff", files.path, "A", 7);
  held.download->verify = [&](const std::string&, std::string&) {
    verifying = true;
    while (!cancelled) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return true;
  };
  require(queue.start(held), "a download whose check waits");
  require(until([&] { return verifying.load(); }), "its body arrived");
  check(queue.cancel("A").size() == 1, "cancel catches it running");
  cancelled = true;
  require(queue.start(download(server.origin, "/riff", files.second, "B", 8)) && wait(queue, result),
          "the next transfer");
  check(result.id == 8 && !exists(files.path), "a download cancelled while it ran leaves no file");
}

void twoTransfersPerOwner(Server& server) {
  const Files files;
  TransferQueue queue(2);
  TransferResult result;
  server.hold();
  TransferRequest first = download(server.origin, "/slow", files.path, "A", 9), second = first, third = first;
  second.id = 10;
  third.id = 11;
  check(queue.start(first) && queue.start(second) && !queue.start(third),
        "two transfers per owner, the third is refused");
  require(until([&] { return server.entered.load(); }), "the first one runs");
  const auto dropped = queue.cancel("A");
  check(dropped.size() == 2 && dropped[0] == 9 && dropped[1] == 10,
        "cancel drops the running and the queued transfer: " + std::to_string(dropped.size()));
  server.release = true;
  require(queue.start(download(server.origin, "/riff", files.second, "B", 12)) && wait(queue, result),
          "the next transfer");
  check(result.id == 12, "a cancelled transfer reports nothing: " + std::to_string(result.id));
}

}

int main() {
  Server server;
  destructionEndsAStalledDownload(server);
  theWorkerStartsWithTheFirstTransfer(server);
  unpolledResultsLoseTheirFiles(server);
  uploads(server);
  theBudgetCoversTheFirstByte(server);
  downloads(server);
  aDownloadStopsWhenItsBytesNoLongerFit(server);
  redirects(server);
  theWorkerFillsHeadersRightBeforeSending(server);
  cancelTakesBackAnUnpolledResult(server);
  cancelCatchesARunningCheck(server);
  twoTransfersPerOwner(server);
  return awtrix::test::finish("linux-transfer-queue");
}
