#include "../support.h"
// The TCP line client against local servers.
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdexcept>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

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
  int fd = -1;
  uint16_t port = 0;
  Server() {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    listen(fd, 8);
    socklen_t len = sizeof a;
    getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    port = ntohs(a.sin_port);
  }
  ~Server() { ::close(fd); }
  int accept() {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, 3000) <= 0) return -1;
    const int c = ::accept(fd, nullptr, nullptr);
    timeval timeout{3, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    return c;
  }
};

std::string readLine(int c) {
  std::string s;
  char ch;
  while (::read(c, &ch, 1) == 1 && ch != '\n') s += ch;
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
  pollfd listener{server.fd, POLLIN, 0};
  if (::poll(&listener, 1, 500) <= 0) {
    check(false, "the script opens a TCP connection");
    return;
  }
  const int client = ::accept(server.fd, nullptr, nullptr);
  check(client >= 0, "the script's connection reaches the server");
  if (client < 0) return;
  ::send(client, "hello\n", 6, MSG_NOSIGNAL);
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
  pollfd closed{client, POLLIN, 0};
  char byte = 0;
  check(::poll(&closed, 1, 500) > 0 && ::recv(client, &byte, 1, 0) == 0,
        "removing the script closes its TCP connection");
  ::close(client);
}

void echoesLines() {
  Server s;
  TcpClients t;
  const uint32_t id = t.connect("A", "127.0.0.1", s.port, 2000);
  check(id != 0, "a connection starts");
  check(t.send("A", id, "hello\n"), "data queued before it opens is accepted");
  const int c = s.accept();
  auto ev = until(t, TcpEvent::Kind::Open);
  check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Open && ev.back().id == id, "it opens");
  check(readLine(c) == "hello", "and the queued line arrives");
  const std::string reply = "one\r\ntwo\npart";
  check(::send(c, reply.data(), reply.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(reply.size()), "server sends reply");
  ev = until(t, TcpEvent::Kind::Line);
  auto more = until(t, TcpEvent::Kind::Line);
  check(!ev.empty() && !more.empty() && ev.back().data == "one" && more.back().data == "two", "lines arrive split, CR stripped");
  ::close(c);
  ev = until(t, TcpEvent::Kind::Closed);
  check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Closed && ev.back().data == "closed",
        "the peer closing is reported, and an unfinished line is dropped");
  check(!t.send("A", id, "x\n"), "a closed connection takes nothing");
}

void refusedAndDns() {
  uint16_t port;
  {
    Server s;
    port = s.port;
  }
  TcpClients t;
  t.connect("A", "127.0.0.1", port, 2000);
  auto ev = until(t, TcpEvent::Kind::Closed);
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
  const int c = s.accept();
  until(t, TcpEvent::Kind::Open);
  const std::string big(20000, 'x');
  check(::send(c, big.data(), big.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(big.size()), "server sends oversized line");
  auto ev = until(t, TcpEvent::Kind::Closed);
  check(!ev.empty() && ev.back().data == "overflow", "a line over 16 KiB closes the connection");
  ::close(c);
}

void forgetIsSilent() {
  Server s;
  TcpClients t;
  t.connect("A", "127.0.0.1", s.port, 2000);
  const int c = s.accept();
  until(t, TcpEvent::Kind::Open);
  t.forget("A");
  char ch;
  check(::read(c, &ch, 1) == 0, "forgetting an app closes its sockets");
  ::send(c, "late\n", 5, MSG_NOSIGNAL);
  auto ev = until(t, TcpEvent::Kind::Line, 300);
  check(ev.empty(), "and nothing more arrives for it");
  ::close(c);
}

void finalLineAndSilentClose() {
  Server s;
  TcpClients t;
  const auto id = t.connect("A", "127.0.0.1", s.port, 2000);
  const int c = s.accept();
  until(t, TcpEvent::Kind::Open);
  check(::send(c, "last\n", 5, MSG_NOSIGNAL) == 5, "final server line sent");
  ::close(c);
  auto ev = until(t, TcpEvent::Kind::Closed);
  check(ev.size() == 2 && ev[0].kind == TcpEvent::Kind::Line && ev[0].data == "last" &&
        ev[1].kind == TcpEvent::Kind::Closed, "EOF preserves complete lines before close");
  t.close("A", id);
  const auto second = t.connect("A", "127.0.0.1", s.port, 2000);
  const int peer = s.accept();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  t.close("B", second);
  check(t.send("A", second, "ok\n"), "another app cannot close a socket");
  t.close("A", second);
  check(until(t, TcpEvent::Kind::Closed, 100).empty(), "explicit close clears queued callbacks");
  ::close(peer);
}

void queueAndNulAreBounded() {
  for (const bool nul : {false, true}) {
    Server s;
    TcpClients t;
    t.connect("A", "127.0.0.1", s.port, 2000);
    const int c = s.accept();
    until(t, TcpEvent::Kind::Open);
    std::string data;
    if (nul) data = std::string("bad\0line\n", 9);
    else for (int i = 0; i < 2000; ++i) data += "x\n";
    check(::send(c, data.data(), data.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(data.size()), "flood sent");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto ev = until(t, TcpEvent::Kind::Closed);
    check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Closed &&
          ev.back().data == (nul ? "error" : "overflow"), "flood and embedded NUL close predictably");
    check(ev.size() <= TcpClients::kInMax / TcpClients::kEventCost + 1, "queue stays bounded and retains close");
    ::close(c);
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
  const int c = s.accept();
  auto ev = until(t, TcpEvent::Kind::Open, 1000);
  check(!ev.empty() && ev.back().kind == TcpEvent::Kind::Open && ev.back().id == fast,
        "a connection opens while another name is still resolving");
  ::send(c, "hi\n", 3, MSG_NOSIGNAL);
  ev = until(t, TcpEvent::Kind::Line, 1000);
  check(!ev.empty() && ev.back().data == "hi" &&
            std::chrono::steady_clock::now() - start < std::chrono::milliseconds(800),
        "and its lines arrive without waiting for the lookup");
  ev = until(t, TcpEvent::Kind::Open, 3000);
  check(!ev.empty() && ev.back().id == slow, "the slow name still connects when it resolves");
  ::close(c);
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
  const int ca = s.accept();
  until(t, TcpEvent::Kind::Open);
  const auto b = t.connect("B", "127.0.0.1", s.port, 2000);
  const int cb = s.accept();
  until(t, TcpEvent::Kind::Open);
  std::string flood;
  for (int i = 0; i < 300; ++i) flood += "a\n";
  ::send(ca, flood.data(), flood.size(), MSG_NOSIGNAL);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ::send(cb, "b\n", 2, MSG_NOSIGNAL);
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
  ::close(ca);
  ::close(cb);
}

void overflowStaysWithItsConnection() {
  Server s;
  TcpClients t;
  const auto a = t.connect("A", "127.0.0.1", s.port, 2000);
  const int ca = s.accept();
  until(t, TcpEvent::Kind::Open);
  const auto b = t.connect("B", "127.0.0.1", s.port, 2000);
  const int cb = s.accept();
  until(t, TcpEvent::Kind::Open);
  std::string flood;
  for (int i = 0; i < 2000; ++i) flood += "a\n";
  ::send(ca, flood.data(), flood.size(), MSG_NOSIGNAL);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  ::send(cb, "b\n", 2, MSG_NOSIGNAL);
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
  ::close(ca);
  ::close(cb);
}

}

void resolverContracts() {
  using namespace awtrix::posix;
  auto failed = Resolver::start([]() -> Resolver::Addresses { throw std::runtime_error("DNS failure"); });
  check(bool(failed), "resolver starts");
  if (!failed) return;
  pollfd completed{failed->fd(), POLLIN, 0};
  check(::poll(&completed, 1, 3000) == 1, "exceptional lookup still wakes the poller");
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
  check(connection.fd.valid() && next == 2, "connect skips an invalid address and advances its cursor");
  if (!connection.fd.valid()) return;
  check((::fcntl(connection.fd.get(), F_GETFD) & FD_CLOEXEC) &&
        (::fcntl(connection.fd.get(), F_GETFL) & O_NONBLOCK), "connect descriptor is non-blocking and close-on-exec");
  const int accepted = server.accept();
  check(accepted >= 0, "fallback connection reaches the local server");
  if (accepted >= 0) ::close(accepted);
}

int main() {
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
