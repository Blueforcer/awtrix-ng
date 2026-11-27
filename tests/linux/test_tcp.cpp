#include "../support.h"
// The TCP line client against local servers.
#if !defined(_WIN32)
#include <fcntl.h>
#endif
#include <stdexcept>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../Sockets.h"
#include "platform/linux/script/TcpClients.h"
#include "platform/linux/script/TcpScripting.h"
#include "platform/linux/script/ExtensionHost.h"
#include "core/apps/AppRegistry.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"

using namespace awtrix::linux_script;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

struct Server {
  awtrix::os::Socket fd = awtrix::os::kNoSocket;
  uint16_t port = 0;
  Server() {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    listen(fd, 8);
    socklen_t len = sizeof a;
    getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
  }
  ~Server() { awtrix::os::closeSocket(fd); }
  awtrix::os::Socket accept() {
    awtrix::os::PollEntry p{fd, POLLIN, 0};
    if (awtrix::os::pollSockets(&p, 1, 3000) <= 0) return awtrix::os::kNoSocket;
    const awtrix::os::Socket c = ::accept(fd, nullptr, nullptr);
    awtrix::test::receiveTimeout(c, 3000);
    return c;
  }
};

std::string readLine(awtrix::os::Socket c) {
  std::string s;
  char ch;
  while (awtrix::os::receiveBytes(c, &ch, 1) == 1 && ch != '\n') s += ch;
  return s;
}

std::vector<TcpEvent> until(TcpClients& t, TcpEvent::Kind kind, int ms = 3000) {
  std::vector<TcpEvent> out;
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    TcpEvent e;
    while (t.pop(e)) {
      out.push_back(e);
      if (e.kind == kind) return out;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return out;
}

void scriptCallbacksAndRemovalUseThePlatformAdapter() {
  Server server;
  TcpScripting tcp;
  awtrix::AppRegistry registry;
  awtrix::script::ScriptServices services;
  awtrix::script::ScriptHost host(registry, services, nullptr, nullptr);
  awtrix::script::ExtensionHost extensions(host, {&tcp});
  const std::string source = "import tcp\nclass App\nvar got\ndef init() self.got = '' end\n"
      "def setup() tcp.connect('127.0.0.1', " + std::to_string(server.port) +
      ", def(kind, data) if kind == 'line' self.got = data end end, {'timeout':500}) end\n"
      "def draw() end\ndef check() return self.got end\nend\nreturn App()";
  check(host.set("S", source) && host.errorOf("S").empty(), "a script imports the registered TCP module");
  awtrix::os::PollEntry listener{server.fd, POLLIN, 0};
  if (awtrix::os::pollSockets(&listener, 1, 500) <= 0) {
    check(false, "the script opens a TCP connection");
    return;
  }
  const awtrix::os::Socket client = ::accept(server.fd, nullptr, nullptr);
  check(client != awtrix::os::kNoSocket, "the script's connection reaches the server");
  if (client == awtrix::os::kNoSocket) return;
  awtrix::os::sendBytes(client, "hello\n", 6);
  std::string received;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  while (std::chrono::steady_clock::now() < deadline && received != "hello") {
    awtrix::RenderCtx ctx;
    host.tick(ctx, "S");
    auto* app = static_cast<awtrix::script::ScriptApp*>(registry.find("S"));
    if (app) app->callCheckForTest(received);
    if (received != "hello") std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(received == "hello", "TCP events reach the script callback through the adapter");
  host.remove("S");
  awtrix::os::PollEntry closed{client, POLLIN, 0};
  char byte = 0;
  check(awtrix::os::pollSockets(&closed, 1, 500) > 0 && awtrix::os::receiveBytes(client, &byte, 1) == 0,
        "removing the script closes its TCP connection");
  awtrix::os::closeSocket(client);
}

void echoesLines() {
  Server s;
  TcpClients t;
  const uint32_t id = t.connect("A", "127.0.0.1", s.port, 2000);
  check(id != 0, "a connection starts");
  check(t.send("A", id, "hello\n"), "data queued before it opens is accepted");
  const awtrix::os::Socket c = s.accept();
  auto ev = until(t, TcpEvent::Kind::Open);
  check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Open && ev.back().id == id, "it opens");
  check(readLine(c) == "hello", "and the queued line arrives");
  const std::string reply = "one\r\ntwo\npart";
  check(awtrix::os::sendBytes(c, reply.data(), reply.size()) == static_cast<long>(reply.size()), "server sends reply");
  ev = until(t, TcpEvent::Kind::Line);
  auto more = until(t, TcpEvent::Kind::Line);
  check(!ev.empty() && !more.empty() && ev.back().data == "one" && more.back().data == "two", "lines arrive split, CR stripped");
  awtrix::os::closeSocket(c);
  ev = until(t, TcpEvent::Kind::Closed);
  check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Closed && ev.back().data == "closed",
        "the peer closing is reported, and an unfinished line is dropped");
  check(!t.send("A", id, "x\n"), "a closed connection takes nothing");
}

#if defined(_WIN32)
constexpr int kRefusedWithinMs = 5000;
#else
constexpr int kRefusedWithinMs = 2000;
#endif

void refusedAndDns() {
  uint16_t port;
  {
    Server s;
    port = s.port;
  }
  TcpClients t;
  t.connect("A", "127.0.0.1", port, kRefusedWithinMs);
  auto ev = until(t, TcpEvent::Kind::Closed, kRefusedWithinMs + 1000);
  check(!ev.empty() && ev.back().data == "refused", "nothing listening is refused");
  t.connect("A", "no-such-host.invalid", 80, 2000);
  ev = until(t, TcpEvent::Kind::Closed, 10000);
  check(!ev.empty() && ev.back().data == "dns", "an unknown name is a dns failure");
}

void limits() {
  Server s;
  TcpClients t;
  std::vector<uint32_t> ids;
  for (int i = 0; i < 5; ++i) ids.push_back(t.connect("A", "127.0.0.1", s.port, 2000));
  check(ids[3] != 0 && ids[4] == 0, "an app holds four connections");
  check(t.connect("B", "127.0.0.1", s.port, 2000) != 0, "another app still gets one");
  check(!t.send("B", ids[0], "x\n"), "an app cannot write to another app's connection");
  check(!t.send("A", ids[0], std::string(70000, 'x')), "more than 64 KiB waiting is refused");
}

void overflow() {
  Server s;
  TcpClients t;
  t.connect("A", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket c = s.accept();
  until(t, TcpEvent::Kind::Open);
  const std::string big(20000, 'x');
  check(awtrix::os::sendBytes(c, big.data(), big.size()) == static_cast<long>(big.size()), "server sends oversized line");
  auto ev = until(t, TcpEvent::Kind::Closed);
  check(!ev.empty() && ev.back().data == "overflow", "a line over 16 KiB closes the connection");
  awtrix::os::closeSocket(c);
}

void forgetIsSilent() {
  Server s;
  TcpClients t;
  t.connect("A", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket c = s.accept();
  until(t, TcpEvent::Kind::Open);
  t.forget("A");
  char ch;
  check(awtrix::os::receiveBytes(c, &ch, 1) == 0, "forgetting an app closes its sockets");
  awtrix::os::sendBytes(c, "late\n", 5);
  auto ev = until(t, TcpEvent::Kind::Line, 300);
  check(ev.empty(), "and nothing more arrives for it");
  awtrix::os::closeSocket(c);
}

void finalLineAndSilentClose() {
  Server s;
  TcpClients t;
  const auto id = t.connect("A", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket c = s.accept();
  until(t, TcpEvent::Kind::Open);
  check(awtrix::os::sendBytes(c, "last\n", 5) == 5, "final server line sent");
  awtrix::os::closeSocket(c);
  auto ev = until(t, TcpEvent::Kind::Closed);
  check(ev.size() == 2 && ev[0].kind == TcpEvent::Kind::Line && ev[0].data == "last" &&
        ev[1].kind == TcpEvent::Kind::Closed, "EOF preserves complete lines before close");
  t.close("A", id);
  const auto second = t.connect("A", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket peer = s.accept();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  t.close("B", second);
  check(t.send("A", second, "ok\n"), "another app cannot close a socket");
  t.close("A", second);
  check(until(t, TcpEvent::Kind::Closed, 100).empty(), "explicit close clears queued callbacks");
  awtrix::os::closeSocket(peer);
}

void queueAndNulAreBounded() {
  for (const bool nul : {false, true}) {
    Server s;
    TcpClients t;
    t.connect("A", "127.0.0.1", s.port, 2000);
    const awtrix::os::Socket c = s.accept();
    until(t, TcpEvent::Kind::Open);
    std::string data;
    if (nul) data = std::string("bad\0line\n", 9);
    else for (int i = 0; i < 2000; ++i) data += "x\n";
    check(awtrix::os::sendBytes(c, data.data(), data.size()) == static_cast<long>(data.size()), "flood sent");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto ev = until(t, TcpEvent::Kind::Closed);
    check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Closed &&
          ev.back().data == (nul ? "error" : "overflow"), "flood and embedded NUL close predictably");
    check(ev.size() <= TcpClients::kInMax / TcpClients::kEventCost + 1, "queue stays bounded and retains close");
    awtrix::os::closeSocket(c);
  }
}

// A lookup that answers loopback at once, and slow.test only after `delay`.
TcpClients::Lookup slowLookup(int delayMs) {
  return [delayMs](const std::string& host, uint16_t port) {
    if (host == "slow.test") std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    return TcpClients::systemLookup("127.0.0.1", port);
  };
}

void slowLookupHoldsUpNobody() {
  Server s;
  TcpClients t(slowLookup(1500));
  const auto slow = t.connect("A", "slow.test", s.port, 5000);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto start = std::chrono::steady_clock::now();
  const auto fast = t.connect("B", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket c = s.accept();
  auto ev = until(t, TcpEvent::Kind::Open, 1000);
  check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Open && ev.back().id == fast,
        "a connection opens while another name is still resolving");
  awtrix::os::sendBytes(c, "hi\n", 3);
  ev = until(t, TcpEvent::Kind::Line, 1000);
  check(!ev.empty() && ev.back().data == "hi" &&
            std::chrono::steady_clock::now() - start < std::chrono::milliseconds(800),
        "and its lines arrive without waiting for the lookup");
  ev = until(t, TcpEvent::Kind::Open, 3000);
  check(!ev.empty() && ev.back().id == slow, "the slow name still connects when it resolves");
  awtrix::os::closeSocket(c);
}

void deadlineCoversTheLookup() {
  Server s;
  TcpClients t(slowLookup(1500));
  const auto start = std::chrono::steady_clock::now();
  t.connect("A", "slow.test", s.port, 200);
  const auto ev = until(t, TcpEvent::Kind::Closed, 1000);
  check(!ev.empty() && ev.back().data == "timeout" &&
            std::chrono::steady_clock::now() - start < std::chrono::milliseconds(700),
        "the timeout ends a connection whose name has not resolved");
}

void closingDoesNotWaitForALookup() {
  const auto start = std::chrono::steady_clock::now();
  {
    Server s;
    TcpClients t(slowLookup(1500));
    t.connect("A", "slow.test", s.port, 5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(600),
        "the clients shut down while a lookup is still running");
}

void connectionsTakeTurns() {
  Server s;
  TcpClients t;
  const auto a = t.connect("A", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket ca = s.accept();
  until(t, TcpEvent::Kind::Open);
  const auto b = t.connect("B", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket cb = s.accept();
  until(t, TcpEvent::Kind::Open);
  std::string flood;
  for (int i = 0; i < 300; ++i) flood += "a\n";
  awtrix::os::sendBytes(ca, flood.data(), flood.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  awtrix::os::sendBytes(cb, "b\n", 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  int position = -1;
  TcpEvent e;
  for (int i = 0; i < 400 && t.pop(e); ++i)
    if (e.id == b && e.data == "b") {
      position = i;
      break;
    }
  check(position >= 0 && position <= 2, "a line on one connection is not queued behind another's backlog");
  (void)a;
  awtrix::os::closeSocket(ca);
  awtrix::os::closeSocket(cb);
}

void overflowStaysWithItsConnection() {
  Server s;
  TcpClients t;
  const auto a = t.connect("A", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket ca = s.accept();
  until(t, TcpEvent::Kind::Open);
  const auto b = t.connect("B", "127.0.0.1", s.port, 2000);
  const awtrix::os::Socket cb = s.accept();
  until(t, TcpEvent::Kind::Open);
  std::string flood;
  for (int i = 0; i < 2000; ++i) flood += "a\n";
  awtrix::os::sendBytes(ca, flood.data(), flood.size());
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  awtrix::os::sendBytes(cb, "b\n", 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  bool aOverflowed = false, bLine = false, bClosed = false;
  TcpEvent e;
  while (t.pop(e)) {
    if (e.id == a && e.kind == TcpEvent::Kind::Closed) aOverflowed = e.data == "overflow";
    if (e.id == b && e.kind == TcpEvent::Kind::Line) bLine = e.data == "b";
    if (e.id == b && e.kind == TcpEvent::Kind::Closed) bClosed = true;
  }
  check(aOverflowed, "the flooding connection overflows");
  check(bLine && !bClosed && t.send("B", b, "ok\n"), "the other connection keeps running");
  awtrix::os::closeSocket(ca);
  awtrix::os::closeSocket(cb);
}

}

void resolverContracts() {
  using namespace awtrix::os;
  auto failed = Resolver::start([]() -> Resolver::Addresses { throw std::runtime_error("DNS failure"); });
  check(bool(failed), "resolver starts");
  if (!failed) return;
  awtrix::os::PollEntry completed{failed->handle(), POLLIN, 0};
  check(awtrix::os::pollSockets(&completed, 1, 3000) == 1, "exceptional lookup still wakes the poller");
  Resolver::Addresses addresses;
  check(failed->take(addresses) && addresses.empty() && !failed->take(addresses), "failed lookup is an empty once-only answer");
  Server server;
  addresses = Resolver::lookup("127.0.0.1", std::to_string(server.port), 1);
  check(addresses.size() == 1, "bounded numeric lookup");
  SocketAddress invalid;
  invalid.family = AF_INET;
  invalid.size = sizeof(sockaddr_storage) + 1;
  addresses.insert(addresses.begin(), invalid);
  std::size_t next = 0;
  auto connection = connectNext(addresses, next);
  check(connection.socket.valid() && next == 2, "connect skips an invalid address and advances its cursor");
  if (!connection.socket.valid()) return;
#if !defined(_WIN32)
  check((::fcntl(connection.socket.get(), F_GETFD) & FD_CLOEXEC) &&
        (::fcntl(connection.socket.get(), F_GETFL) & O_NONBLOCK), "connect descriptor is non-blocking and close-on-exec");
#endif
  const awtrix::os::Socket accepted = server.accept();
  check(accepted != awtrix::os::kNoSocket, "fallback connection reaches the local server");
  if (accepted != awtrix::os::kNoSocket) awtrix::os::closeSocket(accepted);
}

void aThreadOnlyForAConnection() {
  const std::size_t before = awtrix::test::threadCount();
  {
    TcpClients unused(TcpClients::systemLookup);
    unused.forget("nobody");
    TcpEvent event;
    check(!unused.pop(event) && !unused.send("nobody", 1, "x"), "without a connection there is nothing to do");
    check(awtrix::test::threadCount() == before, "and no thread does it");
  }
  check(awtrix::test::threadCount() == before, "ending it joins nothing");
  TcpClients clients([](const std::string&, uint16_t) { return std::vector<TcpAddress>{}; });
  check(clients.connect("", "host", 1, 100) == 0, "a connection that is refused at once");
  check(awtrix::test::threadCount() == before, "starts no thread");
  const uint32_t id = clients.connect("app", "host", 1, 1000);
  check(id != 0 && awtrix::test::threadCount() > before, "the first connection does");
  TcpEvent event;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!clients.pop(event) && std::chrono::steady_clock::now() < end)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  check(event.kind == TcpEvent::Kind::Closed && event.data == "dns", "and it serves it");
}

int main() {
  awtrix::test::require(awtrix::os::startSockets(), "sockets");
  aThreadOnlyForAConnection();
  resolverContracts();
  scriptCallbacksAndRemovalUseThePlatformAdapter();
  echoesLines();
  refusedAndDns();
  limits();
  overflow();
  forgetIsSilent();
  finalLineAndSilentClose();
  queueAndNulAreBounded();
  slowLookupHoldsUpNobody();
  deadlineCoversTheLookup();
  closingDoesNotWaitForALookup();
  connectionsTakeTurns();
  overflowStaysWithItsConnection();
  return failures == 0 ? 0 : 1;
}
