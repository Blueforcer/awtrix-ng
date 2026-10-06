#include "../../../support.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/wifi/SupplicantConfig.h"
#include "platform/tc002/daemon/wifi/WifiService.h"

using namespace awtrix::tc002d;
using awtrix::tc002::WifiLink;

namespace {

int& failures = awtrix::test::failures();
unsigned checks = 0;

using awtrix::test::check;

std::string readText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

class FakeSupplicant {
 public:
  ~FakeSupplicant() { stop(); }

  bool start(const std::string& path) {
    stop();
    const std::string directory = path.substr(0, path.rfind('/'));
    mkdir(directory.c_str(), 0700);
    fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof address.sun_path, "%s", path.c_str());
    unlink(path.c_str());
    if (fd_ < 0 || bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
      std::perror("fake supplicant bind");
      return false;
    }
    path_ = path;
    monitorKnown_ = false;
    commands.clear();
    return true;
  }

  void stop() {
    if (fd_ >= 0) close(fd_);
    if (!path_.empty()) unlink(path_.c_str());
    fd_ = -1;
    path_.clear();
    monitorKnown_ = false;
  }

  bool running() const { return fd_ >= 0; }

  void serve() {
    if (fd_ < 0) return;
    for (;;) {
      char buffer[4096];
      sockaddr_un from{};
      socklen_t size = sizeof from;
      const ssize_t n = recvfrom(fd_, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr*>(&from), &size);
      if (n <= 0) return;
      const std::string command(buffer, static_cast<std::size_t>(n));
      commands.push_back(command);
      std::string reply;
      if (command == "ATTACH") { monitor_ = from; monitorSize_ = size; monitorKnown_ = true; reply = "OK\n"; }
      else if (command == "STATUS") reply = status;
      else if (command == "SIGNAL_POLL") reply = "RSSI=-47\nLINKSPEED=65\nNOISE=9999\nFREQUENCY=2437\n";
      else if (command == "PING") reply = "PONG\n";
      else if (command == "SCAN") reply = scanReply;
      else if (command == "SCAN_RESULTS") reply = scanResults;
      else if (command == "STA-FIRST") {
        reply = stationReply;
        if (clientOnStationQuery) event("<3>AP-STA-CONNECTED 00:11:22:33:44:55");
      }
      else reply = "UNKNOWN COMMAND\n";
      if (!silent) sendto(fd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from), size);
    }
  }

  bool event(const std::string& message) {
    if (fd_ < 0 || !monitorKnown_) return false;
    return sendto(fd_, message.data(), message.size(), 0, reinterpret_cast<sockaddr*>(&monitor_), monitorSize_) ==
           static_cast<ssize_t>(message.size());
  }

  unsigned count(const std::string& command) const {
    unsigned n = 0;
    for (const auto& c : commands) n += c == command;
    return n;
  }

  std::vector<std::string> commands;
  std::string status = "wpa_state=SCANNING\n";
  std::string scanReply = "OK\n";
  std::string stationReply = "FAIL\n";
  bool clientOnStationQuery = false;
  std::string scanResults =
      "bssid / frequency / signal level / flags / ssid\n"
      "00:11:22:33:44:01\t2412\t-71\t[WPA2-PSK-CCMP][ESS]\tHome\n"
      "00:11:22:33:44:02\t2437\t-48\t[WPA2-PSK-CCMP][ESS]\tHome\n"
      "00:11:22:33:44:03\t2462\t-60\t[ESS]\tCaf\\xc3\\xa9\n"
      "00:11:22:33:44:04\t2462\t-30\t[WPA2-PSK-CCMP][ESS]\t\n";
  bool silent = false;

 private:
  int fd_ = -1;
  std::string path_;
  sockaddr_un monitor_{};
  socklen_t monitorSize_ = 0;
  bool monitorKnown_ = false;
};

class FakeSystem : public wifi::WifiSystem {
 public:
  struct Exit { pid_t pid; int status; };

  bool interfacePresent() override { return interface; }
  std::string interfaceAttribute(const char* name) override {
    return std::strcmp(name, "address") == 0 && interface ? mac : std::string();
  }
  bool setInterfaceUp(int&) override { ++upCalls; return true; }

  pid_t loadModule(const std::string& path, const std::string& parameters) override {
    modules.push_back(path);
    events.push_back("module " + path + " [" + parameters + "]");
    const pid_t pid = nextPid++;
    exits.push_back({pid, moduleStatus << 8});
    if (moduleStatus == 0 && contains(path, "fdrv")) interface = true;
    return pid;
  }

  pid_t setProperty(const std::string& key, const std::string& value) override {
    properties.push_back(key + "=" + value);
    if (key == "service.adb.tcp.port" && !tcpPortStuck) tcpDisabled = value == "-1";
    if (key == "ctl.stop" && value == "adbd") { adbd = false; adbTcp = false; }
    if (key == "ctl.start" && value == "adbd") { adbd = true; adbTcp = !tcpDisabled; }
    if (key == "ctl.stop" && value == "wpa_supplicant" && !stockIgnoresStop) stock = false;
    const pid_t pid = nextPid++;
    exits.push_back({pid, 0});
    return pid;
  }

  int writeKernelAttribute(const std::string& path, const std::string& value) override {
    events.push_back("write " + path + "=" + value);
    if (attributeError) return attributeError;
    attributes[path] = value;
    return 0;
  }

  int clearKernelLog() override {
    events.push_back("clear kernel log");
    return 0;
  }

  pid_t startSupplicant(const std::string& program, const std::string& configPath) override {
    events.push_back("supplicant " + program);
    programs.push_back(program);
    const std::string config = readText(configPath);
    configs.push_back(config);
    if (contains(config, "\tmode=2\n")) supplicant.status = "wpa_state=COMPLETED\nmode=AP\n";
    else if (contains(supplicant.status, "mode=AP")) supplicant.status = "wpa_state=SCANNING\n";
    const std::size_t at = config.find("ctrl_interface=");
    const std::string directory = config.substr(at + 15, config.find('\n', at) - at - 15);
    live = nextPid++;
    supplicants.push_back(live);
    if (bindControl) supplicant.start(directory + "/wlan0");
    return live;
  }

  pid_t runTask(const std::function<int()>& task) override {
    if (taskSpawnFails) return -1;
    ++taskRuns;
    const pid_t pid = nextPid++;
    const int code = taskFailure ? taskFailure : task();
    (holdTasks ? heldTasks : exits).push_back({pid, (code & 0xff) << 8});
    return pid;
  }

  void releaseTasks() {
    exits.insert(exits.end(), heldTasks.begin(), heldTasks.end());
    heldTasks.clear();
  }

  std::vector<wifi::ProcessInfo> processes(const char* comm) override {
    std::vector<wifi::ProcessInfo> out;
    if (std::strcmp(comm, "adbd") == 0 && adbd) out.push_back({600, "/sbin/adbd"});
    if (std::strcmp(comm, "wpa_supplicant") == 0) {
      if (stock) out.push_back({stockPid, "/bin/wpa_supplicant -iwlan0 -Dnl80211 -c/data/misc/wifi/wpa_supplicant.conf"});
      if (live > 0) out.push_back({live, "wpa_supplicant -Dnl80211 -iwlan0 -c/tmp/x"});
    }
    return out;
  }

  int tcpListeners(unsigned port) override {
    if (tcpUnreadable) return -1;
    return port == 5555 && adbTcp ? 1 : 0;
  }

  void signal(pid_t pid, int number) override {
    if (pid <= 0) return;
    signals.push_back({pid, number});
    if (pid == live && !ignoreTerm) crash(number);
    if (pid == live && number == SIGKILL) crash(number);
    if (pid == stockPid && (number == SIGKILL || (number == SIGTERM && !stockIgnoresTerm))) stock = false;
  }

  int64_t nowMs() override { return clock ? *clock : 0; }

  void crash(int number) {
    if (live <= 0) return;
    exits.push_back({live, number});
    live = -1;
    supplicant.stop();
  }

  bool interface = false;
  std::string mac = "02:00:00:00:00:07";
  int upCalls = 0;
  int moduleStatus = 0;
  bool adbd = true, adbTcp = true, tcpDisabled = false, tcpPortStuck = false, tcpUnreadable = false;
  bool stock = true, stockIgnoresStop = false, stockIgnoresTerm = false;
  pid_t stockPid = 77;
  pid_t live = -1;
  bool bindControl = true, ignoreTerm = false;
  int64_t* clock = nullptr;
  pid_t nextPid = 1000;
  std::vector<Exit> exits, heldTasks;
  bool holdTasks = false, taskSpawnFails = false;
  int taskFailure = 0;
  unsigned taskRuns = 0;
  std::vector<std::string> modules, properties, configs, programs;
  std::vector<std::string> events;
  std::map<std::string, std::string> attributes;
  int attributeError = 0;
  std::vector<pid_t> supplicants;
  std::vector<std::pair<pid_t, int>> signals;
  FakeSupplicant supplicant;
};

class Registry : public ControlRegistry {
 public:
  void add(std::string command, Handler handler) override { handlers[command] = std::move(handler); }
  std::string call(const std::string& command, std::string_view payload) {
    auto it = handlers.find(command);
    return it == handlers.end() ? std::string("missing") : it->second(payload);
  }
  std::map<std::string, Handler> handlers;
};

const std::string kSupplicant = "/data/awtrix-ng/current/bin/wpa_supplicant";
const std::string kModules = "/data/awtrix-ng/current/lib/modules";

class Harness {
 public:
  explicit Harness(const std::string& root, bool keepAdbTcp = false, const std::string& supplicantPath = kSupplicant,
                   const std::string& moduleDirectory = kModules)
      : root_(root) {
    system = new FakeSystem();
    system->clock = &now;
    WifiOptions options;
    options.stateDir = root + "/state/network";
    options.runDir = root + "/run";
    options.keepAdbTcp = keepAdbTcp;
    options.supplicantPath = supplicantPath;
    options.moduleDirectory = moduleDirectory;
    awtrix::tc002::NetworkStatus network;
    network.ipv4 = "10.0.0.2";
    network.hostname = "awtrix-test";
    state.setNetwork(network);
    state.onNetwork([this]() { ++notifications; });
    service.reset(new WifiService(state, options, registry, std::unique_ptr<wifi::WifiSystem>(system)));
  }

  void step(int64_t advance = 10) {
    now += advance;
    std::vector<FakeSystem::Exit> exits;
    exits.swap(system->exits);
    for (const auto& exit : exits) check(service->onChildExit(exit.pid, exit.status, now), "own child claimed");
    std::vector<PollInterest> interest;
    service->pollInterest(interest);
    std::vector<pollfd> fds;
    for (const auto& item : interest) fds.push_back({item.fd, item.events, 0});
    if (!fds.empty() && poll(fds.data(), fds.size(), 0) > 0)
      for (const auto& item : fds)
        if (item.revents) service->onReady(item.fd, item.revents, now);
    const int64_t deadline = service->nextDeadlineMs();
    if (deadline >= 0 && deadline <= now) { ++timeCalls; service->onTime(now); }
    system->supplicant.serve();
  }

  void run(int64_t duration) {
    for (int64_t elapsed = 0; elapsed < duration; elapsed += 10) step();
  }

  const awtrix::tc002::NetworkStatus& network() const { return state.network(); }

  std::string root_;
  DeviceState state;
  Registry registry;
  FakeSystem* system;
  std::unique_ptr<WifiService> service;
  int64_t now = 100000;
  unsigned notifications = 0;
  unsigned timeCalls = 0;
};

std::string makeRoot(const char* name) {
  std::string pattern = std::string("/tmp/tc002-wifi-") + name + "-XXXXXX";
  const char* made = mkdtemp(&pattern[0]);
  return made ? made : "";
}

void removeTree(const std::string& root) {
  const std::string command = "rm -rf '" + root + "'";
  if (std::system(command.c_str()) != 0) std::fprintf(stderr, "cleanup of %s failed\n", root.c_str());
}

void testColdBootProvisionAndConnect() {
  const std::string root = makeRoot("coldboot");
  Harness h(root);
  check(h.service->start(h.now), "cold start");
  h.run(4000);
  const std::vector<std::string> expected = {"service.adb.tcp.port=-1", "ctl.stop=adbd", "ctl.start=adbd",
                                             "ctl.stop=wpa_supplicant"};
  check(h.system->properties == expected, "adb policy then stock supplicant stop, in order");
  check(h.system->modules.size() == 2 && h.system->modules[0] == kModules + "/aic8800_bsp.ko" &&
            h.system->modules[1] == kModules + "/aic8800_fdrv.ko", "the release's bsp then fdrv loaded");
  check(h.system->upCalls == 1, "wlan0 brought up once");
  check(h.system->supplicants.size() == 1, "setup access point without credentials");
  check(!h.service->configured(), "no stored credentials: Wi-Fi is not configured");
  check(h.network().link == WifiLink::AccessPoint && h.network().mac == "02:00:00:00:00:07" &&
            h.network().ssid == "awtrix-test", "access point with hostname and mac");
  check(h.network().ipv4 == "10.0.0.2" && h.network().hostname == "awtrix-test", "ip fields left untouched");
  const unsigned idleCalls = h.timeCalls;
  h.run(5000);
  check(h.timeCalls == idleCalls, "access point waits for status timer without spinning");
  std::string status = h.registry.call("wifi-status", "");
  check(contains(status, "\"adb\":\"usb-only\"") && contains(status, "\"link\":\"access-point\"") &&
            contains(status, "\"adbTcpListeners\":0"), "status reports usb-only adb");

  const std::string secret = "correct horse battery";
  const std::string reply =
      h.registry.call("wifi-set", "{\"ssid\":\"Home \\\"Net\\\"\",\"password\":\"correct horse battery\"}");
  check(reply == "{\"ok\":true}", "wifi-set accepted");
  check(!contains(reply, secret), "reply never echoes the password");
  h.run(1500);
  check(h.system->supplicants.size() == 2, "station started after provisioning reply grace period");
  check(h.service->configured(), "provisioned: Wi-Fi is configured");
  const wifi::WpaPsk psk = wifi::deriveWpaPsk(secret, "Home \"Net\"");
  const std::string config = h.system->configs.empty() ? std::string() : h.system->configs.back();
  check(contains(config, "\tssid=" + awtrix::posix::hexBytes("Home \"Net\"", 10) + "\n") &&
            contains(config, "\tpsk=" + awtrix::posix::hexBytes(psk.data(), psk.size()) + "\n") &&
            contains(config, "ctrl_interface=" + root + "/run/wpa/ctrl\n"), "generated config");
  check(!contains(config, secret), "config never holds the passphrase");
  struct stat info {};
  check(stat((root + "/run/wpa/wpa.conf").c_str(), &info) == 0 && (info.st_mode & 07777) == 0600, "config is 0600");
  const std::string stored = readText(root + "/state/network/wifi.cred");
  check(!stored.empty() && !contains(stored, secret), "store written without the passphrase");
  check(h.system->supplicant.count("ATTACH") == 1 && h.system->supplicant.count("STATUS") >= 1, "attached + status");
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Home \"Net\"", "connecting");

  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Home \\\"Net\\\"\n";
  check(h.system->supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:55 completed [id=0 id_str=]"),
        "event sent");
  h.run(200);
  check(h.network().link == WifiLink::Connected && h.network().rssi == -47, "connected with rssi");
  const unsigned polls = h.system->supplicant.count("SIGNAL_POLL");
  const unsigned connectedCalls = h.timeCalls;
  h.run(10500);
  check(h.timeCalls - connectedCalls <= 6, "connected link wakes only for its timers");
  check(h.system->supplicant.count("SIGNAL_POLL") > polls, "signal polled periodically");
  std::vector<awtrix::tc002::WifiNetwork> connectedScan;
  bool scanned = false;
  h.service->scan([&](const std::vector<awtrix::tc002::WifiNetwork>& networks) {
    connectedScan = networks;
    scanned = true;
  });
  h.run(100);
  check(h.system->supplicants.size() == 2 && h.system->supplicant.count("SCAN") == 1 && !scanned,
        "scan uses the running supplicant and waits for its event");
  h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS ");
  h.run(100);
  check(scanned && connectedScan.size() == 2 && h.network().link == WifiLink::Connected, "scan while connected");
  status = h.registry.call("wifi-status", "");
  check(contains(status, "\"link\":\"connected\"") && !contains(status, secret) &&
            !contains(status, awtrix::posix::hexBytes(psk.data(), psk.size())), "status is secret-free");

  h.system->supplicant.event("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=3");
  h.run(50);
  check(h.network().link == WifiLink::Disconnected && h.network().rssi == 0, "disconnected");
  h.system->supplicant.event(
      "<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Home\" auth_failures=1 duration=10 reason=WRONG_KEY");
  h.run(50);
  check(h.network().link == WifiLink::Failed, "wrong key is failed");
  h.system->supplicant.status = "wpa_state=SCANNING\n";
  h.system->supplicant.event("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=15");
  h.run(50);
  check(h.network().link == WifiLink::Failed, "failed survives later disconnects");
  h.system->supplicant.status = "wpa_state=COMPLETED\n";
  h.system->supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:55 completed");
  h.run(50);
  check(h.network().link == WifiLink::Connected, "recovered after a later success");

  const pid_t first = h.system->live;
  h.system->crash(SIGSEGV);
  h.run(500);
  check(h.network().link == WifiLink::Disconnected && h.system->supplicants.size() == 2, "crash: down, backing off");
  h.run(1000);
  check(h.system->supplicants.size() == 3 && h.system->live != first, "crashed supplicant restarted after backoff");
  h.run(200);
  check(h.system->supplicant.count("ATTACH") == 1, "fresh control connection after restart");
  check(contains(h.registry.call("wifi-status", ""), "\"supplicantRestarts\":1"), "restart counted");

  h.system->supplicant.status = "wpa_state=SCANNING\n";
  const std::string storedBefore = readText(root + "/state/network/wifi.cred");
  const std::size_t keepBefore = h.system->supplicants.size();
  check(h.service->setCredentials({"Home \"Net\"", ""}), "same ssid without a password accepted");
  h.run(300);
  check(readText(root + "/state/network/wifi.cred") == storedBefore, "same ssid + empty password keeps the key");
  check(h.system->supplicants.size() == keepBefore + 1 &&
            contains(h.system->configs.back(), "\tpsk=" + awtrix::posix::hexBytes(psk.data(), psk.size()) + "\n"),
        "reconnected with the stored key");

  const std::size_t before = h.system->supplicants.size();
  const pid_t running = h.system->live;
  check(h.registry.call("wifi-set", "{\"ssid\":\"Second\",\"password\":\"\"}") == "{\"ok\":true}",
        "re-provision to an open network");
  h.run(300);
  bool terminated = false;
  for (const auto& s : h.system->signals) terminated = terminated || (s.first == running && s.second == SIGTERM);
  check(terminated && h.system->supplicants.size() == before + 1, "running supplicant replaced");
  check(contains(h.system->configs.back(), "\tkey_mgmt=NONE\n") &&
            contains(h.system->configs.back(), "\tssid=" + awtrix::posix::hexBytes("Second", 6) + "\n"), "open config");
  check(h.network().ssid == "Second" && h.network().link == WifiLink::Connecting, "new network connecting");
  check(contains(h.registry.call("wifi-status", ""), "\"supplicantRestarts\":1"), "intentional restart not counted");

  const pid_t last = h.system->live;
  h.service->requestStop(h.now);
  check(!h.service->stopped(), "stop waits for the supplicant");
  h.run(50);
  bool termSent = false;
  for (const auto& s : h.system->signals) termSent = termSent || (s.first == last && s.second == SIGTERM);
  check(termSent && h.service->stopped(), "stopped after supplicant exit");
  check(access((root + "/run/wpa/wpa.conf").c_str(), F_OK) != 0, "config removed on stop");
  check(!h.service->setCredentials({"Late", "password1"}), "no credentials accepted while stopping");
  removeTree(root);
}

void testInvalidInput() {
  const std::string root = makeRoot("invalid");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start");
  h.run(100);
  check(!h.service->setCredentials({"", "password1"}), "empty ssid");
  check(!h.service->setCredentials({std::string(33, 'x'), "password1"}), "long ssid");
  check(!h.service->setCredentials({"x", "short"}), "short password");
  check(!h.service->setCredentials({"x", std::string(64, 'z')}), "non-hex 64");
  check(!h.service->setCredentials({"x", "tab\there!"}), "control character");
  check(h.registry.call("wifi-set", "not json") == "{\"ok\":false,\"error\":\"malformed\"}", "malformed");
  check(h.registry.call("wifi-set", "{\"ssid\":1,\"password\":\"password1\"}") ==
            "{\"ok\":false,\"error\":\"malformed\"}", "ssid type");
  check(h.registry.call("wifi-set", "{\"ssid\":\"x\"}") == "{\"ok\":false,\"error\":\"malformed\"}",
        "password required");
  const std::string invalid = h.registry.call("wifi-set", "{\"ssid\":\"x\",\"password\":\"sh0rt\"}");
  check(invalid == "{\"ok\":false,\"error\":\"invalid-password\"}" && !contains(invalid, "sh0rt"),
        "invalid password not echoed");
  check(h.registry.call("wifi-set", "{\"ssid\":\"" + std::string(33, 'x') + "\",\"password\":\"password1\"}") ==
            "{\"ok\":false,\"error\":\"invalid-ssid\"}", "invalid ssid");
  check(h.system->properties.empty() && h.system->modules.empty(), "keepAdbTcp + present wlan0 touch nothing");
  h.run(200);
  check(h.system->supplicants.size() == 1 && h.network().link == WifiLink::AccessPoint, "still in setup access point");
  check(contains(h.registry.call("wifi-status", ""), "\"adb\":\"kept\""), "adb kept");
  removeTree(root);
}

void testStoredCredentialsAndStuckStock() {
  const std::string root = makeRoot("stored");
  {
    wifi::WifiProfile profile;
    wifi::makeProfile("Stored", "password1", profile);
    int error = 0;
    wifi::CredentialStore(root + "/state/network").save(profile, error);
  }
  Harness h(root);
  h.system->adbTcp = false;
  h.system->interface = true;
  h.system->stockIgnoresStop = true;
  h.system->stockIgnoresTerm = true;
  check(h.service->start(h.now), "start");
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Stored", "stored credentials: connecting");
  h.run(2000);
  check(h.system->properties.size() == 2 && h.system->properties[0] == "service.adb.tcp.port=-1" &&
            h.system->properties[1] == "ctl.stop=wpa_supplicant", "adb not restarted when tcp is already closed");
  check(h.system->supplicants.empty(), "supplicant waits for the stock one to go");
  h.run(5000);
  bool term = false, kill = false;
  for (const auto& s : h.system->signals) {
    term = term || (s.first == 77 && s.second == SIGTERM);
    kill = kill || (s.first == 77 && s.second == SIGKILL);
  }
  check(term && kill, "stuck stock supplicant escalated to SIGTERM then SIGKILL");
  check(h.system->supplicants.size() == 1, "own supplicant started after the stock one is gone");
  check(h.system->modules.empty(), "present wlan0: no module load");
  removeTree(root);
}

void testControlInterfaceTimeout() {
  const std::string root = makeRoot("noctrl");
  {
    wifi::WifiProfile profile;
    wifi::makeProfile("Stored", "password1", profile);
    int error = 0;
    wifi::CredentialStore(root + "/state/network").save(profile, error);
  }
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  h.system->bindControl = false;
  check(h.service->start(h.now), "start");
  h.run(9000);
  check(h.system->supplicants.size() == 1, "one supplicant while waiting for its socket");
  h.run(1500);
  bool killed = false;
  for (const auto& s : h.system->signals) killed = killed || s.second == SIGKILL;
  check(killed, "supplicant without control socket is killed");
  h.system->bindControl = true;
  h.run(2000);
  check(h.system->supplicants.size() == 2, "and restarted");
  h.run(100);
  check(h.system->supplicant.count("ATTACH") == 1, "control reached after restart");
  check(!contains(h.registry.call("wifi-status", ""), "\"error\":\"\""), "wifi-status names the failure");
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Stored\n";
  h.system->supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:01 completed");
  h.run(300);
  check(h.network().link == WifiLink::Connected && contains(h.registry.call("wifi-status", ""), "\"error\":\"\""),
        "a connection clears it");
  removeTree(root);
}

void testTamperedStoreAndDriverFailure() {
  const std::string root = makeRoot("tamper");
  mkdir((root + "/state").c_str(), 0700);
  mkdir((root + "/state/network").c_str(), 0700);
  {
    const int fd = open((root + "/state/network/wifi.cred").c_str(), O_WRONLY | O_CREAT, 0600);
    if (write(fd, "AWNGWIFI garbage", 16) != 16) std::perror("write");
    close(fd);
  }
  Harness h(root, true);
  h.system->stock = false;
  h.system->moduleStatus = ENOENT;
  check(h.service->start(h.now), "start");
  h.run(200);
  const std::string status = h.registry.call("wifi-status", "");
  check(contains(status, "\"store\":\"invalid\"") && contains(status, "\"configured\":false"), "invalid store reported");
  check(h.service->configured(), "an unreadable credential store still means a network is expected");
  check(contains(status, "\"stage\":\"backoff\"") && contains(status, "aic8800_bsp.ko"), "module failure backs off");
  h.system->moduleStatus = 0;
  h.run(1500);
  check(h.system->modules.size() == 3 && h.network().link == WifiLink::AccessPoint, "driver retried and setup AP loaded");
  removeTree(root);
}

}

bool signalled(const FakeSystem& system, pid_t pid, int number) {
  for (const auto& s : system.signals)
    if (s.first == pid && s.second == number) return true;
  return false;
}

void testScanWithoutCredentials() {
  const std::string root = makeRoot("scan-ap");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start");
  h.run(300);
  check(h.network().link == WifiLink::AccessPoint, "setup AP ready");
  unsigned answers = 0;
  h.service->scan([&](const std::vector<awtrix::tc002::WifiNetwork>& networks) {
    ++answers;
    check(networks.empty(), "no fabricated scan results in AP mode");
  });
  check(answers == 1, "AP scan callback answers promptly");
  check(contains(h.registry.call("wifi-scan", ""), "scan-unavailable-in-access-point"), "AP scan explicit failure");
  check(contains(h.registry.call("wifi-scan-results", ""), "scan-unavailable-in-access-point"), "scan limitation reported");
  h.run(180000);
  check(h.system->supplicants.size() == 1 && h.network().link == WifiLink::AccessPoint &&
            h.system->supplicant.count("SCAN") == 0 && h.system->supplicant.count("STA-FIRST") == 0,
        "AP remains up indefinitely without stored credentials");
  removeTree(root);
}

void testStationScanSharingAndTimeout() {
  const std::string root = makeRoot("scan-station");
  wifi::WifiProfile profile;
  wifi::makeProfile("Home", "password1", profile);
  int error = 0;
  wifi::CredentialStore(root + "/state/network").save(profile, error);
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Home\n";
  check(h.service->start(h.now), "start connected station");
  h.run(300);
  unsigned answers = 0;
  const auto collect = [&](const std::vector<awtrix::tc002::WifiNetwork>& networks) {
    ++answers;
    check(networks.size() == 2 && networks[0].ssid == "Home", "scan results deduplicated and sorted");
  };
  h.service->scan(collect);
  h.service->scan(collect);
  h.run(300);
  check(h.system->supplicant.count("SCAN") == 1 && answers == 0, "concurrent callers share one scan");
  h.run(10500);
  check(answers == 2 && h.system->supplicant.count("SCAN_RESULTS") == 1, "scan event timeout fetches results");
  h.system->supplicant.scanReply = "FAIL-BUSY\n";
  h.service->scan(collect);
  h.run(100);
  h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS ");
  h.run(100);
  check(answers == 3 && h.network().link == WifiLink::Connected, "busy scan completes without losing the link");
  removeTree(root);
}

void testSilentSupplicantKeepsScanCache() {
  const std::string root = makeRoot("scan-silent");
  wifi::WifiProfile profile;
  wifi::makeProfile("Home", "password1", profile);
  int error = 0;
  wifi::CredentialStore(root + "/state/network").save(profile, error);
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Home\n";
  check(h.service->start(h.now), "start connected station");
  h.run(300);
  std::vector<awtrix::tc002::WifiNetwork> cached;
  h.service->scan([&](const auto& networks) { cached = networks; });
  h.run(100);
  h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS ");
  h.run(100);
  check(!cached.empty(), "completed scan has cached results");

  h.system->supplicant.silent = true;
  unsigned answers = 0;
  h.service->scan([&](const auto& networks) {
    ++answers;
    check(networks.size() == cached.size() && !networks.empty() && networks[0].ssid == cached[0].ssid,
          "failed scan returns the previous completed results");
  });
  h.run(22000);
  check(answers == 1, "scan give-up answers its caller once");
  check(h.system->supplicants.size() > 1, "unanswered commands restart the supplicant");
  h.system->supplicant.silent = false;
  h.run(30000);
  check(h.network().link == WifiLink::Connected || h.network().link == WifiLink::AccessPoint,
        "command session recovers and reports the active network mode after replies resume");
  removeTree(root);
}

void testEraseCredentials() {
  const std::string root = makeRoot("erase");
  {
    wifi::WifiProfile profile;
    wifi::makeProfile("Stored", "password1", profile);
    int error = 0;
    wifi::CredentialStore(root + "/state/network").save(profile, error);
  }
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start");
  h.run(300);
  const pid_t running = h.system->live;
  check(running > 0 && h.network().ssid == "Stored", "network supplicant running");
  const std::string record = root + "/state/network/wifi.cred";
  h.service->eraseCredentials();
  check(access(record.c_str(), F_OK) != 0 && access((root + "/run/wpa/wpa.conf").c_str(), F_OK) != 0,
        "record and config removed");
  check(h.network().link == WifiLink::Unconfigured && h.network().ssid.empty(), "link unconfigured at once");
  h.run(200);
  check(signalled(*h.system, running, SIGTERM) && h.system->supplicants.size() == 2 && h.system->live > 0 &&
            h.network().link == WifiLink::AccessPoint, "station replaced by setup AP");
  const std::string status = h.registry.call("wifi-status", "");
  check(contains(status, "\"configured\":false") && contains(status, "\"link\":\"access-point\"") &&
            contains(status, "\"store\":\"missing\""), "status after erase");
  check(!h.service->configured(), "erased credentials: Wi-Fi is not configured");

  check(h.registry.call("wifi-set", "{\"ssid\":\"Late\",\"password\":\"password1\"}") == "{\"ok\":true}",
        "credentials in flight");
  h.service->eraseCredentials();
  h.run(200);
  check(access(record.c_str(), F_OK) != 0 && h.system->supplicants.size() == 3 &&
            h.network().link == WifiLink::AccessPoint, "erase wins over a write still in flight");
  removeTree(root);
}

void storeProfile(const std::string& root, const char* ssid) {
  wifi::WifiProfile profile;
  wifi::makeProfile(ssid, "password1", profile);
  int error = 0;
  wifi::CredentialStore(root + "/state/network").save(profile, error);
}

void testAccessPointFallbackAndRetry() {
  const std::string root = makeRoot("ap-fallback");
  storeProfile(root, "Home");
  const std::string stored = readText(root + "/state/network/wifi.cred");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start unavailable router");
  h.run(14000);
  check(h.network().link == WifiLink::Connecting && h.system->supplicants.size() == 1,
        "station gets its initial fifteen second attempt");
  h.run(1500);
  check(h.network().link == WifiLink::AccessPoint && h.network().ssid == "awtrix-test" &&
            h.system->supplicants.size() == 2, "unavailable router falls back to setup AP");
  check(readText(root + "/state/network/wifi.cred") == stored && h.service->configured(),
        "fallback retains saved credentials");
  h.system->supplicant.stationReply = "00:11:22:33:44:55\nflags=[AUTH][ASSOC]\n";
  h.system->supplicant.event("<3>AP-STA-CONNECTED 00:11:22:33:44:55");
  h.run(125000);
  check(h.system->supplicants.size() == 2 && h.network().link == WifiLink::AccessPoint &&
            h.system->supplicant.count("STA-FIRST") >= 2, "connected provisioner is never removed for router retries");
  h.system->supplicant.stationReply = "UNKNOWN COMMAND\n";
  h.run(65000);
  check(h.system->supplicants.size() == 2, "unknown station inventory never removes the AP");
  h.system->supplicant.stationReply = "FAIL\n";
  h.system->supplicant.clientOnStationQuery = true;
  h.run(65000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 2,
        "client event queued with an empty inventory cancels router retry");
  h.system->supplicant.clientOnStationQuery = false;
  h.system->supplicant.event("<3>AP-STA-DISCONNECTED 00:11:22:33:44:55");
  h.run(59000);
  check(h.network().link == WifiLink::AccessPoint, "disconnected provisioner gets a quiet retry interval");
  h.run(3000);
  check(h.network().link == WifiLink::Connecting && h.system->supplicants.size() == 3,
        "verified empty AP retries stored router");
  h.run(16000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 4 &&
            readText(root + "/state/network/wifi.cred") == stored, "failed router retry restores AP without losing credentials");
  h.system->holdTasks = true;
  h.system->taskFailure = ENOSPC;
  check(h.service->setCredentials({"New", "password2"}), "credentials accepted while AP active");
  check(h.network().link == WifiLink::AccessPoint, "saving does not remove the setup connection");
  h.system->releaseTasks();
  h.run(2000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 4 &&
            readText(root + "/state/network/wifi.cred") == stored, "failed persistence leaves AP and old profile intact");
  h.system->holdTasks = false;
  h.system->taskFailure = 0;
  check(h.service->setCredentials({"New", "password2"}), "new profile accepted");
  h.run(500);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 4,
        "persisted profile waits for provisioning reply delivery");
  h.run(1000);
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "New" && h.system->supplicants.size() == 5,
        "new profile then starts station mode");
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=New\n";
  h.system->supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection completed");
  h.run(30000);
  check(h.network().link == WifiLink::Connected && h.system->supplicants.size() == 5,
        "successful station cancels fallback");
  h.system->supplicant.status = "wpa_state=SCANNING\n";
  h.system->supplicant.event("<3>CTRL-EVENT-DISCONNECTED reason=3");
  h.run(14000);
  check(h.network().link == WifiLink::Disconnected, "router loss gives station time to reconnect");
  h.run(2000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 6,
        "router loss restores setup AP after fifteen seconds");
  removeTree(root);
}

void testAccessPointNameAndReadiness() {
  const std::string root = makeRoot("ap-name");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  auto network = h.state.network();
  network.hostname.clear();
  h.state.setNetwork(network);
  check(h.service->start(h.now), "start default AP name");
  check(h.network().link == WifiLink::Unconfigured, "AP is not announced before supplicant confirmation");
  h.run(300);
  check(h.network().link == WifiLink::AccessPoint && h.network().ssid == "awtrixng-000007",
        "AP defaults to the device MAC hostname");
  h.system->supplicant.event("<3>AP-DISABLED");
  h.run(100);
  check(h.network().link == WifiLink::Unconfigured, "disabled AP withdraws network availability");
  h.system->supplicant.event("<3>AP-ENABLED");
  h.run(100);
  check(h.network().link == WifiLink::AccessPoint, "enabled AP publishes availability");
  removeTree(root);
}

void testAccessPointFallbackWithoutAddress() {
  const std::string root = makeRoot("ap-no-dhcp");
  storeProfile(root, "Home");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  auto network = h.state.network();
  network.ipv4.clear();
  h.state.setNetwork(network);
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Home\n";
  check(h.service->start(h.now), "start associated station without DHCP");
  h.run(29000);
  check(h.network().link == WifiLink::Connected && h.system->supplicants.size() == 1,
        "association gets thirty seconds for an IP address");
  h.run(2000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 2,
        "missing DHCP restores the setup access point");
  check(h.service->setCredentials({"Home", ""}), "retry stored credentials");
  h.run(1500);
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Home\n";
  h.system->supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection completed");
  h.run(1000);
  network = h.state.network();
  network.ipv4 = "192.0.2.7";
  h.state.setNetwork(network);
  h.run(40000);
  check(h.network().link == WifiLink::Connected && h.system->supplicants.size() == 3,
        "received address cancels AP fallback");
  network = h.state.network();
  network.ipv4.clear();
  h.state.setNetwork(network);
  h.run(45000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 4,
        "later DHCP lease loss also restores setup AP");
  removeTree(root);
}

void testAccessPointWaitsForLatestCredentials() {
  const std::string root = makeRoot("ap-pending");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start setup AP");
  h.run(300);
  check(h.service->setCredentials({"First", "password1"}), "first credentials accepted");
  h.run(500);
  h.system->holdTasks = true;
  h.system->taskFailure = ENOSPC;
  check(h.service->setCredentials({"Second", "password2"}), "corrected credentials accepted before transition");
  h.run(2000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 1,
        "new pending write cancels the older delayed AP teardown");
  h.system->releaseTasks();
  h.run(2000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 1 &&
            contains(h.registry.call("wifi-status", ""), "credential store write failed"),
        "failed corrected credentials leave the setup connection available");
  h.system->taskFailure = 0;
  check(h.service->setCredentials({"Third", "password3"}), "next credentials accepted");
  check(h.service->setCredentials({"Latest", "password4"}), "latest credentials queued");
  h.system->releaseTasks();
  h.run(2000);
  check(h.network().link == WifiLink::AccessPoint && h.system->supplicants.size() == 1,
        "queued latest writer also keeps the AP until it finishes");
  h.system->releaseTasks();
  h.run(500);
  check(h.network().link == WifiLink::AccessPoint, "latest persisted request retains reply grace");
  h.run(1000);
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Latest" &&
            h.system->supplicants.size() == 2, "only the latest persisted request starts station mode");
  removeTree(root);
}

unsigned countOf(const std::vector<std::string>& list, const std::string& item) {
  unsigned n = 0;
  for (const auto& entry : list) n += entry == item;
  return n;
}

void testAdbFailClosed() {
  const std::string root = makeRoot("adbfail");
  storeProfile(root, "Stored");
  Harness h(root);
  h.system->interface = true;
  h.system->tcpPortStuck = true;
  check(h.service->start(h.now), "start");
  h.run(2500);
  check(h.system->supplicants.empty(), "no supplicant while adb still listens on tcp");
  check(!h.system->stock, "the stock supplicant is stopped all the same");
  const std::string status = h.registry.call("wifi-status", "");
  check(contains(status, "\"adb\":\"tcp-listening\"") && contains(status, "\"stage\":\"backoff\"") &&
            contains(status, "staying unassociated"), "status reports the open adb port");
  h.run(6000);
  check(countOf(h.system->properties, "service.adb.tcp.port=-1") >= 2 && h.system->supplicants.empty(),
        "adb transition retried, still no association");
  h.system->tcpPortStuck = false;
  h.run(20000);
  check(h.system->supplicants.size() == 1 && !h.system->adbTcp &&
            contains(h.registry.call("wifi-status", ""), "\"adb\":\"usb-only\""),
        "association starts once adb is usb only");
  removeTree(root);

  const std::string unreadable = makeRoot("adbtable");
  storeProfile(unreadable, "Stored");
  Harness t(unreadable);
  t.system->interface = true;
  t.system->stock = false;
  t.system->tcpUnreadable = true;
  check(t.service->start(t.now), "start with an unreadable tcp table");
  t.run(5000);
  check(t.system->supplicants.empty() && contains(t.registry.call("wifi-status", ""), "\"adb\":\"failed\""),
        "unverifiable adb state keeps wi-fi down");
  const std::string scanOnly = makeRoot("adbscan");
  Harness s(scanOnly);
  s.system->interface = true;
  s.system->stock = false;
  s.system->tcpUnreadable = true;
  check(s.service->start(s.now), "start without credentials");
  s.run(2000);
  s.service->scan(nullptr);
  s.run(2000);
  check(s.system->supplicants.empty(), "not even a scan-only supplicant while adb is unverified");
  removeTree(unreadable);
  removeTree(scanOnly);
}

void testSupplicantProgram() {
  const std::string root = makeRoot("program");
  storeProfile(root, "Home");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start");
  h.run(300);
  h.service->eraseCredentials();
  h.run(300);
  h.service->scan(nullptr);
  h.run(300);
  check(h.system->programs.size() == 2 && h.system->programs[0] == kSupplicant && h.system->programs[1] == kSupplicant,
        "network and scan-only supplicants run the release's binary");
  check(!h.system->configs.empty() &&
            contains(h.system->configs[0], "\tkey_mgmt=WPA-PSK WPA-PSK-SHA256\n\tieee80211w=1\n"),
        "the release's binary is offered WPA-PSK-SHA256 and optional PMF");
  removeTree(root);

  const std::string relative = makeRoot("relative-program");
  Harness r(relative, true, "bin/wpa_supplicant");
  check(!r.service->start(r.now), "relative supplicant path refused");
  Harness n(relative, true, "");
  check(!n.service->start(n.now), "no supplicant path, no Wi-Fi: there is no stock default");
  removeTree(relative);
}

void testHiddenNetworks() {
  const std::string root = makeRoot("hidden");
  storeProfile(root, "Hidden");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start");
  h.run(300);
  check(h.system->supplicants.size() == 1 && !contains(h.system->configs[0], "scan_ssid"),
        "no scan yet: the ssid is not probed for");
  check(h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS "), "supplicant scan event");
  h.run(300);
  check(h.system->supplicants.size() == 2 && contains(h.system->configs.back(), "\tscan_ssid=1\n"),
        "network missing from the supplicant's own scan is probed for as hidden");
  h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS ");
  h.run(300);
  check(h.system->supplicants.size() == 2 && h.system->supplicant.count("SCAN_RESULTS") == 0,
        "a hidden probe is not second-guessed");

  check(h.registry.call("wifi-set", "{\"ssid\":\"Home\",\"password\":\"password1\"}") == "{\"ok\":true}",
        "visible network provisioned");
  h.run(300);
  check(h.system->supplicants.size() == 3 && !contains(h.system->configs.back(), "scan_ssid"),
        "network seen in the last scan is not probed for by name");
  h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS ");
  h.run(300);
  check(h.system->supplicant.count("SCAN_RESULTS") == 1 && h.system->supplicants.size() == 3,
        "visible network keeps its supplicant");
  h.system->supplicant.status = "wpa_state=COMPLETED\nssid=Home\n";
  h.system->supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:02 completed");
  h.run(300);
  h.system->supplicant.event("<3>CTRL-EVENT-SCAN-RESULTS ");
  h.run(300);
  check(h.network().link == WifiLink::Connected && h.system->supplicant.count("SCAN_RESULTS") == 1,
        "no result fetch while connected");

  check(h.registry.call("wifi-set", "{\"ssid\":\"Elsewhere\",\"password\":\"password1\"}") ==
            "{\"ok\":true}", "network absent from the last scan provisioned");
  h.run(300);
  check(h.system->supplicants.size() == 4 && contains(h.system->configs.back(), "\tscan_ssid=1\n"),
        "network absent from the last scan is probed for as hidden");
  removeTree(root);
}

std::size_t eventIndex(const FakeSystem& system, const std::string& event) {
  for (std::size_t i = 0; i < system.events.size(); ++i)
    if (system.events[i] == event) return i;
  return system.events.size();
}

void testDriverLogsNoSessionKeys() {
  const char* level = "/sys/module/aic8800_fdrv/parameters/aicwf_dbg_level";
  const std::string root = makeRoot("keys");
  const std::string logPath = root + "/daemon.log";
  Log::open(logPath);
  Log::mirrorToStderr(false);
  storeProfile(root, "Home");
  {
    Harness h(root, true);
    h.system->stock = false;
    check(h.service->start(h.now), "start");
    check(h.system->events.size() >= 2 && h.system->events[0] == "write /proc/sys/kernel/dmesg_restrict=1" &&
              h.system->events[1] == "clear kernel log",
          "start restricts dmesg and clears the ring buffer before anything else");
    h.run(300);
    check(eventIndex(*h.system, "module " + kModules + "/aic8800_bsp.ko []") < h.system->events.size(),
          "the bsp module is loaded without options");
    const std::size_t fdrv = eventIndex(*h.system, "module " + kModules + "/aic8800_fdrv.ko [aicwf_dbg_level=0]");
    const std::size_t started = eventIndex(*h.system, "supplicant " + kSupplicant);
    check(fdrv < started && started < h.system->events.size(), "the driver is loaded with debug level 0 first");
  }
  {
    Harness h(root, true);
    h.system->interface = true;
    h.system->stock = false;
    check(h.service->start(h.now), "start with the driver already loaded");
    h.run(300);
    const std::size_t quiet = eventIndex(*h.system, std::string("write ") + level + "=0");
    const std::size_t started = eventIndex(*h.system, "supplicant " + kSupplicant);
    check(h.system->modules.empty() && quiet < started && started < h.system->events.size(),
          "a loaded driver gets debug level 0 before the supplicant starts");
  }
  {
    Harness h(root, true);
    h.system->interface = true;
    h.system->stock = false;
    h.system->attributeError = EACCES;
    check(h.service->start(h.now), "start with read-only attributes");
    h.run(300);
    check(h.system->supplicants.size() == 1, "the link still comes up");
  }
  Log::close();
  Log::mirrorToStderr(true);
  const std::string log = readText(logPath);
  check(contains(log, "kernel log: dmesg_restrict=1 set, earlier messages cleared"),
        "the log says the kernel log was restricted and cleared");
  check(contains(log, "cannot set aicwf_dbg_level=0 (Permission denied)"),
        "a failed level change is logged");
  removeTree(root);
}

void testKeepAdbTcpIsLogged() {
  const std::string root = makeRoot("adbsource");
  const std::string logPath = root + "/daemon.log";
  Log::open(logPath);
  Log::mirrorToStderr(false);
  {
    Harness h(root, true);
    h.system->interface = true;
    h.system->stock = false;
    check(h.service->start(h.now), "start with adb kept");
    h.run(100);
    check(h.system->properties.empty(), "adb over tcp is left as it is");
  }
  Log::close();
  Log::mirrorToStderr(true);
  check(contains(readText(logPath), "adb: DEVELOPER MODE (developer flag file): root ADB over Wi-Fi left as it is"),
        "the flag file is named as the source");
  removeTree(root);
}

void testCredentialRequestReportsItsNetwork() {
  const std::string root = makeRoot("request");
  storeProfile(root, "Home");
  Harness h(root, true);
  h.system->interface = true;
  h.system->stock = false;
  check(h.service->start(h.now), "start");
  h.run(300);
  FakeSupplicant& supplicant = h.system->supplicant;
  supplicant.status = "wpa_state=COMPLETED\nssid=Home\n";
  supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:02 completed");
  h.run(300);
  check(h.network().link == WifiLink::Connected && h.network().ssid == "Home" && h.network().rssi == -47,
        "connected to the stored network");
  const std::string record = root + "/state/network/wifi.cred";
  const std::string stored = readText(record);
  const std::size_t started = h.system->supplicants.size();
  const std::string wrongKey =
      "<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Home\" auth_failures=1 duration=10 reason=WRONG_KEY";

  h.system->holdTasks = true;
  h.system->taskFailure = ENOSPC;
  check(h.registry.call("wifi-set", "{\"ssid\":\"Other\",\"password\":\"password2\"}") ==
            "{\"ok\":true}", "request for another network accepted");
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Other" && h.network().rssi == 0,
        "the requested network is connecting before the writer has run");
  std::string status = h.registry.call("wifi-status", "");
  check(contains(status, "\"link\":\"connecting\",\"ssid\":\"Other\",\"rssi\":0") &&
            contains(status, "\"store\":\"saving\""), "wifi-status reports the request while saving");
  supplicant.event(wrongKey);
  h.run(100);
  supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:02 completed");
  h.run(11000);
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Other" && h.network().rssi == 0,
        "the old network's events never reach the request");
  h.system->releaseTasks();
  h.run(100);
  check(readText(record) == stored && h.system->supplicants.size() == started,
        "a failed write leaves the record and the running supplicant alone");
  check(h.network().link == WifiLink::Connected && h.network().ssid == "Home" && h.network().rssi == -47,
        "a failed write reports the network in use again");
  status = h.registry.call("wifi-status", "");
  check(contains(status, "\"ssid\":\"Home\"") && contains(status, "\"store\":\"ok\"") &&
            contains(status, "\"error\":\"credential store write failed: No space left on device\""),
        "wifi-status names the failed write");

  h.system->taskFailure = 0;
  supplicant.status = "wpa_state=SCANNING\n";
  supplicant.event(wrongKey);
  h.run(100);
  check(h.network().link == WifiLink::Failed && h.network().ssid == "Home", "stored key rejected");
  check(h.registry.call("wifi-set", "{\"ssid\":\"Home\",\"password\":\"password3\"}") ==
            "{\"ok\":true}", "new key for the same network accepted");
  check(!contains(h.registry.call("wifi-status", ""), "credential store write failed"),
        "a new request clears the failed write of the one before");
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Home",
        "a stale failure is not reported for the new key");
  supplicant.event(wrongKey);
  h.run(100);
  check(h.network().link == WifiLink::Connecting, "the old key's failure stays out while saving");
  h.system->releaseTasks();
  h.run(300);
  const wifi::WpaPsk psk = wifi::deriveWpaPsk("password3", "Home");
  check(h.system->supplicants.size() == started + 1 &&
            contains(h.system->configs.back(), "\tpsk=" + awtrix::posix::hexBytes(psk.data(), psk.size()) + "\n"),
        "the new key is applied once written");
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Home" &&
            !contains(h.registry.call("wifi-status", ""), "\"store\":\"saving\""),
        "connecting with the new key");

  check(h.registry.call("wifi-set", "{\"ssid\":\"First\",\"password\":\"password1\"}") ==
            "{\"ok\":true}", "first request");
  check(h.registry.call("wifi-set", "{\"ssid\":\"Second\",\"password\":\"password1\"}") ==
            "{\"ok\":true}", "second request while the first is saving");
  check(h.network().ssid == "Second" && h.network().link == WifiLink::Connecting, "the latest request is reported");
  h.system->releaseTasks();
  h.run(300);
  check(h.network().ssid == "Second" && h.network().link == WifiLink::Connecting &&
            contains(h.registry.call("wifi-status", ""), "\"store\":\"saving\""),
        "the latest request stays reported while its own writer runs");
  h.system->releaseTasks();
  h.run(300);
  check(h.network().ssid == "Second" &&
            contains(h.system->configs.back(), "\tssid=" + awtrix::posix::hexBytes("Second", 6) + "\n"),
        "the latest request is applied");

  supplicant.status = "wpa_state=COMPLETED\nssid=Second\n";
  supplicant.event("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:05 completed");
  h.run(300);
  h.system->holdTasks = false;
  h.system->taskSpawnFails = true;
  check(h.registry.call("wifi-set", "{\"ssid\":\"Third\",\"password\":\"password1\"}") ==
            "{\"ok\":false,\"error\":\"rejected\"}", "a writer that cannot start rejects the request");
  check(h.network().link == WifiLink::Connected && h.network().ssid == "Second",
        "a rejected request leaves the reported network alone");
  check(contains(h.registry.call("wifi-status", ""), "\"error\":\"credential writer spawn failed\""),
        "wifi-status names the writer that did not start");

  h.system->taskSpawnFails = false;
  const unsigned runs = h.system->taskRuns;
  const std::size_t before = h.system->supplicants.size();
  check(h.service->setCredentials({"Second", ""}), "same network without a password");
  check(h.network().link == WifiLink::Connecting && h.network().ssid == "Second" && h.system->taskRuns == runs,
        "the shortcut reconnects at once without writing");
  h.run(300);
  check(h.system->supplicants.size() == before + 1, "and restarts the supplicant");
  removeTree(root);
}

void testReleaseDriver() {
  const std::string root = makeRoot("release-driver");
  const std::string logPath = root + "/daemon.log";
  Log::open(logPath);
  Log::mirrorToStderr(false);
  storeProfile(root, "Home");
  {
    Harness h(root, true);
    h.system->stock = false;
    check(h.service->start(h.now), "start with the release driver");
    h.run(300);
    const std::size_t bsp = eventIndex(*h.system, "module " + kModules + "/aic8800_bsp.ko []");
    const std::size_t fdrv = eventIndex(*h.system, "module " + kModules + "/aic8800_fdrv.ko [aicwf_dbg_level=0]");
    check(h.system->modules.size() == 2 && bsp < fdrv && fdrv < h.system->events.size(),
          "the release's bsp, then its fdrv with debug level 0");
    check(h.system->supplicants.size() == 1, "the link comes up on the release driver");
    check(contains(h.registry.call("wifi-status", ""), "\"driver\":\"release\""), "status names the release driver");
  }
  {
    Harness h(root, true);
    h.system->stock = false;
    h.system->moduleStatus = ENOENT;
    check(h.service->start(h.now), "start without the release driver");
    h.run(5000);
    bool others = false;
    for (const std::string& module : h.system->modules) others = others || module.rfind(kModules + "/", 0) != 0;
    check(!h.system->modules.empty() && !others && h.system->supplicants.empty(),
          "a missing release driver is retried from the release only, never replaced");
    check(contains(h.registry.call("wifi-status", ""), "\"stage\":\"backoff\"") &&
              contains(h.registry.call("wifi-status", ""), "aic8800_bsp.ko: No such file or directory"),
          "status names the missing module");
  }
  {
    Harness h(root, true);
    h.system->interface = true;
    h.system->stock = false;
    check(h.service->start(h.now), "warm start with wlan0 present");
    h.run(300);
    check(h.system->modules.empty() && h.system->supplicants.size() == 1, "a loaded driver is kept");
    check(contains(h.registry.call("wifi-status", ""), "\"driver\":\"already loaded\""),
          "status says the driver was loaded before this start");
  }
  {
    Harness h(root, true, kSupplicant, "lib/modules");
    check(!h.service->start(h.now), "a relative module directory is refused");
    Harness n(root, true, kSupplicant, "");
    check(!n.service->start(n.now), "no module directory, no Wi-Fi: there is no stock default");
  }
  Log::close();
  Log::mirrorToStderr(true);
  const std::string log = readText(logPath);
  check(contains(log, "wifi: loading the release aic8800 driver from " + kModules), "the log names the release pair");
  check(contains(log, "wifi: wlan0 exists; keeping the aic8800 driver that is loaded"), "the log says the driver is kept");
  removeTree(root);
}

int main() {
  signal(SIGPIPE, SIG_IGN);
  testColdBootProvisionAndConnect();
  testInvalidInput();
  testStoredCredentialsAndStuckStock();
  testControlInterfaceTimeout();
  testTamperedStoreAndDriverFailure();
  testScanWithoutCredentials();
  testStationScanSharingAndTimeout();
  testSilentSupplicantKeepsScanCache();
  testAccessPointFallbackAndRetry();
  testAccessPointNameAndReadiness();
  testAccessPointFallbackWithoutAddress();
  testAccessPointWaitsForLatestCredentials();
  testEraseCredentials();
  testAdbFailClosed();
  testSupplicantProgram();
  testHiddenNetworks();
  testKeepAdbTcpIsLogged();
  testDriverLogsNoSessionKeys();
  testCredentialRequestReportsItsNetwork();
  testReleaseDriver();
  std::printf("tc002 wifi service: %u checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
