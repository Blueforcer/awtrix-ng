#include "platform/linux/ble/BleService.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "platform/posix/Files.h"

namespace awtrix::ble {

constexpr int kTickMs = 20;
constexpr auto kCallTimeout = std::chrono::milliseconds(1500);

BleService::BleService(Options options, std::function<void(BleEvent)> sink)
    : options_(std::move(options)), sink_(std::move(sink)) {
  LinuxBleRadio::Options radio;
  radio.bondsPath = options_.bondsPath;
  radio.log = [this](const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logLines_.size() < 64) logLines_.push_back(line);
  };
  if (options_.supervised) radio.requestController = [this](bool on) { controllerRequest_.store(on ? 1 : 0); };
  radio_ = std::make_unique<LinuxBleRadio>(std::move(radio));
  radio_->setName(options_.name);
  hub_ = std::make_unique<BleHub>(*radio_, [this](BleEvent e) {
    if (gamepad_->owns(e.script)) gamepad_->onEvent(std::move(e));
    else if (e.script == IphoneLink::kOwner) iphone_->onEvent(std::move(e));
    else sink_(std::move(e));
  });
  hub_->setDeviceName(options_.name);
  hub_->log = [this](const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logLines_.size() < 64) logLines_.push_back(line);
  };
  gamepad_ = std::make_unique<GamepadManager>(*hub_, options_.gamepadPath, hub_->log);
  iphone_ = std::make_unique<IphoneLink>(*hub_, options_.name, hub_->log);
}

BleService::~BleService() { stop(); }

void BleService::start() {
  if (worker_.joinable()) return;
  if (::pipe2(wake_, O_CLOEXEC | O_NONBLOCK) != 0) return;
  worker_ = std::thread([this] { loop(); });
  if (options_.gamepad) post([this] { gamepad_->start(posix::monotonicMs()); });
}

void BleService::stop() {
  if (!worker_.joinable()) return;
  post([this] { radio_->power(false); });
  stopping_ = true;
  post([] {});
  worker_.join();
  ::close(wake_[0]);
  ::close(wake_[1]);
  wake_[0] = wake_[1] = -1;
}

void BleService::post(std::function<void()> run) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push_back(Command{std::move(run)});
  }
  const char byte = 1;
  if (wake_[1] >= 0) (void)!::write(wake_[1], &byte, 1);
}

// The operations that leave something running under the callback id.
static bool holdsResource(const std::string& op) {
  return op == "scan" || op == "connect" || op == "subscribe" || op == "advertise" || op == "serve";
}

std::string BleService::call(const std::string& script, const std::string& op, const std::string& args, uint32_t id) {
  if (!worker_.joinable() || stopping_) return "{\"error\":\"Bluetooth is not running\"}";
  // Either the reply reaches the caller or the worker undoes the operation, never neither: the
  // decision is taken once, under the lock.
  struct Answer {
    std::mutex lock;
    std::condition_variable ready;
    bool abandoned = false;
    bool delivered = false;
    std::string reply;
  };
  auto answer = std::make_shared<Answer>();
  post([this, answer, script, op, args, id] {
    const int64_t now = posix::monotonicMs();
    std::string reply = hub_->call(script, op, args, id, now);
    std::unique_lock<std::mutex> hold(answer->lock);
    if (answer->abandoned) {
      hold.unlock();
      // The script was told "busy" and let go of the callback: nothing may keep running for it.
      if (holdsResource(op) && reply.rfind("{\"error\"", 0) != 0)
        hub_->call(script, "stop", "{\"id\":" + std::to_string(id) + "}", 0, now);
      return;
    }
    answer->reply = std::move(reply);
    answer->delivered = true;
    answer->ready.notify_one();
  });
  std::unique_lock<std::mutex> hold(answer->lock);
  if (answer->ready.wait_for(hold, kCallTimeout, [&] { return answer->delivered; })) return answer->reply;
  answer->abandoned = true;
  return "{\"error\":\"Bluetooth is busy\"}";
}

void BleService::forget(const std::string& script) {
  if (!worker_.joinable()) return;
  post([this, script] { hub_->forget(script, posix::monotonicMs()); });
}

GamepadPairResult BleService::pairGamepad() {
  if (!worker_.joinable() || stopping_ || !options_.gamepad) return {};
  struct Answer {
    std::mutex mutex;
    std::condition_variable ready;
    bool cancelled = false, delivered = false;
    GamepadPairResult result;
  };
  auto answer = std::make_shared<Answer>();
  post([this, answer] {
    std::unique_lock<std::mutex> lock(answer->mutex);
    if (answer->cancelled) return;
    answer->result = gamepad_->pair(posix::monotonicMs());
    answer->delivered = true;
    answer->ready.notify_one();
  });
  std::unique_lock<std::mutex> lock(answer->mutex);
  if (answer->ready.wait_for(lock, kCallTimeout, [&] { return answer->delivered; })) return answer->result;
  answer->cancelled = true;
  return {};
}

void BleService::forgetGamepad(int id) {
  if (worker_.joinable()) post([this, id] { gamepad_->forget(id, posix::monotonicMs()); });
}

void BleService::configureIphone(const IphoneConfig& config) {
  post([this, config] { iphone_->configure(config, posix::monotonicMs()); });
}

void BleService::forgetIphone() {
  post([this] { iphone_->forget(posix::monotonicMs()); });
}

void BleService::controllerStatus(bool on, const std::string& error) {
  post([this, on, error] { radio_->controllerStatus(on, error); });
}

int BleService::takeControllerRequest() { return controllerRequest_.exchange(-1); }

void BleService::drainLog(const std::function<void(const std::string&)>& write) {
  std::deque<std::string> lines;
  {
    std::lock_guard<std::mutex> lock(logMutex_);
    lines.swap(logLines_);
  }
  for (const std::string& line : lines) write(line);
}

void BleService::loop() {
  std::vector<pollfd> fds;
  int64_t lastTick = 0;
  while (!stopping_) {
    fds.clear();
    fds.push_back({wake_[0], POLLIN, 0});
    radio_->collect(fds);
    ::poll(fds.data(), fds.size(), kTickMs);
    const int64_t now = posix::monotonicMs();
    if (fds[0].revents & POLLIN) {
      char buf[64];
      while (::read(wake_[0], buf, sizeof buf) > 0) {
      }
    }
    std::deque<Command> commands;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      commands.swap(commands_);
    }
    // Hub and radio learn the time before any command can start a deadline.
    hub_->tick(now);
    radio_->dispatch(std::vector<pollfd>(fds.begin() + 1, fds.end()), now);
    for (Command& c : commands) c.run();
    gamepad_->tick(now);
    iphone_->tick(now);
    if (now - lastTick >= kTickMs) {
      lastTick = now;
      radio_->tick(now);
    }
  }
  std::deque<Command> commands;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    commands.swap(commands_);
  }
  for (Command& c : commands) c.run();
}

}
