// awtrix-host: the script host for ESP32 clocks. It pairs clocks with a six-digit code, runs one
// awtrix-linux --remote process for each and offers itself to the clocks it finds on the network.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/api/ApiRouter.h"
#include "core/api/HttpProtocol.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/hostd/Children.h"
#include "platform/linux/hostd/ClockStore.h"
#include "platform/linux/hostd/Discovery.h"
#include "platform/linux/hostd/PairingCode.h"
#include "platform/posix/Files.h"
#include "platform/posix/Time.h"

using namespace awtrix;
using namespace awtrix::hostd;
using posix::monotonicMs;

namespace {

constexpr std::size_t kMaxBody = 1024;

std::atomic<bool> g_stop{false};

struct Options {
  std::string state = "/var/lib/awtrix-host";
  std::string binary = "/usr/bin/awtrix-linux";
  std::string webui;
  std::string name;
  // The address clocks and browsers reach this host at, when it differs from the one a request
  // arrives at (Docker bridge networks, NAT).
  std::string advertise;
  int port = 8300;
  bool discovery = true;
};

void usage() {
  std::puts("awtrix-host [--state DIR] [--port 8300] [--binary awtrix-linux] [--webui FILE] [--name NAME]\n"
            "  [--advertise ADDRESS] [--no-discovery]\n"
            "The script host for ESP32 clocks: pair a clock in its web UI under System > Script host with the\n"
            "code this program prints.");
}

bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--no-discovery") { o.discovery = false; continue; }
    if (arg == "--help") { usage(); std::exit(0); }
    if (i + 1 >= argc) return false;
    const std::string value = argv[++i];
    if (arg == "--state") o.state = value;
    else if (arg == "--port") o.port = std::atoi(value.c_str());
    else if (arg == "--binary") o.binary = value;
    else if (arg == "--webui") o.webui = value;
    else if (arg == "--name") o.name = value;
    else if (arg == "--advertise") o.advertise = value;
    else return false;
  }
  return o.port > 0 && o.port < 65536;
}

std::string hostName() {
  char name[256] = {};
  if (::gethostname(name, sizeof(name) - 1) != 0 || !name[0]) return "awtrix-host";
  return name;
}

std::string newKey() {
  std::random_device random;
  uint8_t bytes[32];
  for (uint8_t& byte : bytes) byte = static_cast<uint8_t>(random());
  return posix::hexBytes(bytes, sizeof(bytes));
}

bool portFree(int port) {
  for (const int type : {SOCK_STREAM, SOCK_DGRAM}) {
    const int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));
    const bool free = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    ::close(fd);
    if (!free) return false;
  }
  return true;
}

bool fromThisHost(const httplib::Request& req, int port) {
  if (req.remote_addr != "127.0.0.1") return false;
  const std::string host = req.get_header_value("Host");
  const std::string suffix = ":" + std::to_string(port);
  if (req.get_header_value_count("Host") != 1 || (host != "127.0.0.1" + suffix && host != "localhost" + suffix))
    return false;
  const std::string origin = req.get_header_value("Origin");
  const std::string site = req.get_header_value("Sec-Fetch-Site");
  return !api::fromOtherSite({origin, req.get_header_value_count("Origin"), host, 1, site,
                              req.get_header_value_count("Sec-Fetch-Site")});
}

void allowAnyOrigin(httplib::Response& res) {
  api::corsHeaders([&res](const char* name, const char* value) { res.set_header(name, value); }, false);
}

const char kPage[] = R"(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AWTRIX script host</title><style>body{font:15px system-ui,sans-serif;margin:24px;max-width:640px;color:#222}
h1{font-size:20px}td,th{padding:4px 10px 4px 0;text-align:left}.code{font:600 32px monospace;letter-spacing:4px}
.muted{color:#777}</style></head><body><h1>AWTRIX script host</h1><p id="name" class="muted"></p>
<h2>Pairing code</h2><div id="code"></div><h2>Clocks</h2><table id="clocks"></table>
<script>
async function load(){
  const s=await (await fetch('/api/v1/status')).json();
  document.getElementById('name').textContent=s.name+' · '+s.version;
  const c=document.getElementById('code');
  if(s.code){c.innerHTML='<span class="code"></span> <span class="muted"></span>';
    c.children[0].textContent=s.code;c.children[1].textContent='valid '+Math.ceil(s.codeValidSeconds/60)+' min';}
  else c.innerHTML='<p class="muted">Shown in the log of this host, for example with <code>docker logs awtrix-host</code>.</p>';
  const t=document.getElementById('clocks');t.textContent='';
  if(!s.clocks.length){t.innerHTML='<tr><td class="muted">No clock paired yet.</td></tr>';return;}
  for(const k of s.clocks){const r=t.insertRow();
    for(const v of [k.name||k.uid,k.width+'×'+k.height,k.clock,'port '+k.port,k.running?'running':'restarting'])r.insertCell().textContent=v;}
}
load();setInterval(load,5000);
</script></body></html>)";

}

int main(int argc, char** argv) {
  Options o;
  if (!parse(argc, argv, o)) {
    usage();
    return 2;
  }
  if (o.name.empty()) o.name = hostName();
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGINT, [](int) { g_stop = true; });
  std::signal(SIGTERM, [](int) { g_stop = true; });

  ClockStore store(o.state);
  std::string problem;
  if (!store.load(problem)) {
    std::fprintf(stderr, "%s\n", problem.c_str());
    return 1;
  }
  std::mutex mutex;
  Children children(o.binary, o.webui);
  std::random_device seed;
  std::mt19937 random(seed());
  PairingCode code([&random] { return static_cast<uint32_t>(random()); });
  Discovery discovery;
  if (o.discovery && !discovery.begin(o.name, static_cast<uint16_t>(o.port)))
    std::fputs("discovery: UDP port 4211 is taken, clocks are not offered this host\n", stderr);

  httplib::Server server;
  server.set_default_headers({{"Cache-Control", "no-store"}});
  server.set_payload_max_length(kMaxBody);
  server.Options(R"(/api/v1/pair(/[0-9A-Fa-f]{1,32})?)", [](const httplib::Request&, httplib::Response& res) {
    allowAnyOrigin(res);
    res.set_header("Access-Control-Allow-Methods", "POST, DELETE");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    res.set_header("Access-Control-Max-Age", "600");
    res.status = 204;
  });
  server.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(kPage, "text/html"); });
  server.Get("/api/v1/status", [&](const httplib::Request& req, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(mutex);
    std::string body;
    api::JsonWriter w(body);
    w.beginObject().key("name").value(o.name).key("version").value(AWTRIX_NG_VERSION);
    w.key("clocks").beginArray();
    for (const ClockRecord& clock : store.clocks())
      w.beginObject().key("uid").value(clock.uid).key("name").value(clock.name).key("clock").value(clock.clockUrl)
          .key("width").value(clock.width).key("height").value(clock.height).key("port").value(clock.port)
          .key("running").value(children.running(clock.uid)).endObject();
    w.endArray();
    if (fromThisHost(req, o.port)) {
      const int64_t now = monotonicMs();
      w.key("code").value(code.current(now)).key("codeValidSeconds").value(static_cast<long long>(code.validForMs(now) / 1000));
    }
    w.endObject();
    res.set_content(body, "application/json");
  });
  server.Post("/api/v1/pair", [&](const httplib::Request& req, httplib::Response& res) {
    allowAnyOrigin(res);
    const std::string& json = req.body;
    ClockRecord record;
    record.uid = api::memberText(json, "uid");
    record.name = api::memberText(json, "name");
    record.clockUrl = api::memberText(json, "clock");
    long long width = 0, height = 0;
    api::memberValue(json, "width").asLong(width);
    api::memberValue(json, "height").asLong(height);
    record.width = static_cast<int>(width);
    record.height = static_cast<int>(height);
    const std::string board = api::memberText(json, "board");
    auto refuse = [&res](int status, const char* code, const char* message) {
      res.status = status;
      res.set_content(api::errorJson(code, message), "application/json");
    };
    if (!ClockStore::validUid(record.uid)) return refuse(422, "validationFailed", "uid must be hex digits");
    if (board == "tc002") return refuse(422, "boardNotSupported", "this clock runs its own scripts");
    if (record.height != 8 || record.width < 32 || record.width > 128)
      return refuse(422, "panelNotSupported", "panel must be 8 high, 32 to 128 wide");
    if (record.clockUrl.rfind("http://", 0) != 0 || record.clockUrl.size() > 128)
      return refuse(422, "validationFailed", "clock must be http://address");
    std::lock_guard<std::mutex> lock(mutex);
    switch (code.check(api::memberText(json, "code"), monotonicMs())) {
      case PairingCode::Check::Locked: return refuse(429, "locked", "too many wrong codes");
      case PairingCode::Check::Wrong: return refuse(403, "wrongCode", "wrong code");
      case PairingCode::Check::Ok: break;
    }
    const std::string key = newKey();
    ClockRecord stored;
    std::string why;
    const bool known = store.find(record.uid) != nullptr;
    if (!store.pair(record, key, portFree, stored, why)) return refuse(500, "internalError", why.c_str());
    if (known) children.restart(stored.uid);
    children.sync(store, monotonicMs());
    std::printf("clock %s (%s) paired, port %d\n", stored.uid.c_str(), stored.name.c_str(), stored.port);
    std::fflush(stdout);
    const std::string address = !o.advertise.empty() ? o.advertise
                                : req.local_addr.rfind("::ffff:", 0) == 0 ? req.local_addr.substr(7) : req.local_addr;
    const std::string hostPort = address + ":" + std::to_string(stored.port);
    std::string body;
    api::JsonWriter(body).beginObject().key("url").value("http://" + hostPort).key("scriptHost").value(hostPort)
        .key("key").value(key).key("port").value(stored.port).endObject();
    res.set_content(body, "application/json");
  });
  server.Delete(R"(/api/v1/pair/([0-9A-Fa-f]{1,32}))", [&](const httplib::Request& req, httplib::Response& res) {
    allowAnyOrigin(res);
    const std::string uid = req.matches[1];
    std::lock_guard<std::mutex> lock(mutex);
    if (!store.keyOk(uid, req.get_header_value("Authorization"))) {
      res.status = 401;
      res.set_content(api::errorJson("unauthorized", "the clock's key is needed"), "application/json");
      return;
    }
    children.stop(uid);
    store.remove(uid, req.get_param_value("keep") == "1");
    std::printf("clock %s unpaired\n", uid.c_str());
    std::fflush(stdout);
    res.set_content("{\"ok\":true}", "application/json");
  });

  if (!server.bind_to_port("0.0.0.0", o.port)) {
    std::fprintf(stderr, "cannot listen on port %d\n", o.port);
    return 1;
  }
  std::thread http([&server] { server.listen_after_bind(); });
  {
    std::lock_guard<std::mutex> lock(mutex);
    children.sync(store, monotonicMs());
  }
  std::printf("awtrix-host %s on port %d, %zu clock(s)\n", AWTRIX_NG_VERSION, o.port, store.clocks().size());
  while (!g_stop) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      const int64_t now = monotonicMs();
      children.tick(store, now);
      if (o.discovery) discovery.tick(now, store);
      const std::string& current = code.current(now);
      if (code.takeRenewed()) {
        std::printf("pairing code %s (valid 10 min)\n", current.c_str());
        std::fflush(stdout);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  server.stop();
  http.join();
  std::lock_guard<std::mutex> lock(mutex);
  children.stopAll();
  return 0;
}
