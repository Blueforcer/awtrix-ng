#include "../support.h"

#include <cstdint>
#include <string>
#include <vector>

#include "platform/host/app/HostSupervisor.h"
#include "platform/linux/hostd/Contract.h"

using awtrix::hostapp::HostProcess;
using awtrix::hostapp::HostState;
using awtrix::hostapp::HostSupervisor;
using awtrix::hostapp::StopReason;
using awtrix::test::check;

namespace {

class FakeHost : public HostProcess {
 public:
  bool start(const std::vector<std::string>& args) override {
    ++starts;
    lastArgs = args;
    return !refuse;
  }
  void stop() override { ++stops; }

  int starts = 0;
  int stops = 0;
  bool refuse = false;
  std::vector<std::string> lastArgs;
};

const std::vector<std::string> kArgs = {"awtrix-host", "--exit-with-stdin", "--log", "host.log"};
constexpr int64_t kSettle = HostSupervisor::kSettleMs;
constexpr int64_t kHealthy = HostSupervisor::kHealthyMs;

int64_t delayAfterEnd(HostSupervisor& supervisor, int exitCode, int64_t at) {
  supervisor.ended(exitCode, at);
  return supervisor.wakeAt().value_or(-1) - at;
}

void startsAndSettles() {
  FakeHost host;
  HostSupervisor supervisor(host, kArgs);
  check(supervisor.state() == HostState::Stopped && supervisor.reason() == StopReason::None && !supervisor.wakeAt(),
        "nothing runs and nothing is planned before start");
  supervisor.ended(1, 100);
  check(supervisor.state() == HostState::Stopped && !supervisor.wakeAt() && host.starts == 0,
        "an end reported before any start changes nothing");

  supervisor.start(1000);
  check(host.starts == 1 && host.lastArgs == kArgs, "start launches the host with its arguments");
  check(supervisor.state() == HostState::Starting && supervisor.wakeAt() == 1000 + kSettle,
        "it is starting, with one wake-up for when it counts as running");
  check(supervisor.view(1500).up() && supervisor.view(1500).restartInMs == 0, "a starting host is up");
  supervisor.wake(1000 + kSettle - 1);
  check(supervisor.state() == HostState::Starting, "an early wake-up changes nothing");
  supervisor.start(1500);
  check(host.starts == 1, "a second start while it runs starts nothing");
  supervisor.wake(1000 + kSettle);
  check(supervisor.state() == HostState::Running && !supervisor.wakeAt(),
        "alive for the settle time it runs, and no wake-up is left");
  supervisor.wake(500000);
  check(supervisor.state() == HostState::Running && host.starts == 1, "a stray wake-up leaves a running host alone");
}

void backsOff() {
  FakeHost host;
  HostSupervisor supervisor(host, kArgs);
  supervisor.start(0);
  supervisor.wake(kSettle);

  check(delayAfterEnd(supervisor, 1, 10000) == 5000, "an unexpected end restarts after 5 s");
  check(supervisor.state() == HostState::Restarting && !supervisor.view(10000).up() &&
            supervisor.view(10000).restartInMs == 5000 && supervisor.view(14000).restartInMs == 1000,
        "restarting, with the time left");
  supervisor.ended(1, 11000);
  check(supervisor.wakeAt() == 15000, "an end reported while restarting changes nothing");
  supervisor.wake(14999);
  check(host.starts == 1 && supervisor.state() == HostState::Restarting, "not before its time");
  supervisor.wake(15000);
  check(host.starts == 2 && supervisor.state() == HostState::Starting, "then it starts again");

  check(delayAfterEnd(supervisor, 1, 16000) == 10000, "a second end soon after waits 10 s");
  supervisor.wake(26000);
  check(delayAfterEnd(supervisor, 1, 27000) == 60000, "a third 60 s");
  supervisor.wake(87000);
  check(delayAfterEnd(supervisor, 0, 88000) == 60000, "and every one after it 60 s, exit code 0 included");

  supervisor.wake(148000);
  supervisor.wake(148000 + kSettle);
  check(delayAfterEnd(supervisor, 1, 148000 + kHealthy - 1) == 60000, "just under a minute of running is not enough");
  const int64_t restarted = 148000 + kHealthy - 1 + 60000;
  supervisor.wake(restarted);
  supervisor.wake(restarted + kSettle);
  check(delayAfterEnd(supervisor, 3, restarted + kHealthy) == 5000, "a minute without trouble starts over at 5 s");
  check(host.stops == 0, "ends of the host never call stop");
}

void portBusy() {
  FakeHost host;
  HostSupervisor supervisor(host, kArgs);
  supervisor.start(0);
  supervisor.ended(awtrix::hostd::kExitPortBusy, 300);
  check(supervisor.state() == HostState::Stopped && supervisor.reason() == StopReason::PortBusy &&
            !supervisor.wakeAt() && !supervisor.view(300).up(),
        "a taken port stops the host with its own reason and plans no restart");
  supervisor.wake(200000);
  check(host.starts == 1, "a wake-up does not start it");
  supervisor.start(300000);
  check(host.starts == 2 && supervisor.state() == HostState::Starting && supervisor.reason() == StopReason::None,
        "start tries again and clears the reason");

  FakeHost later;
  HostSupervisor failing(later, kArgs);
  failing.start(0);
  failing.ended(1, 100);
  failing.wake(5100);
  failing.ended(1, 5200);
  failing.wake(15200);
  failing.ended(awtrix::hostd::kExitPortBusy, 15300);
  check(failing.state() == HostState::Stopped && failing.reason() == StopReason::PortBusy,
        "a taken port after crashes stops it too");
  failing.start(20000);
  check(delayAfterEnd(failing, 1, 20500) == 5000, "a start by hand starts the delays over");
}

void quits() {
  FakeHost host;
  HostSupervisor supervisor(host, kArgs);
  supervisor.start(0);
  supervisor.wake(kSettle);
  supervisor.quit();
  check(host.stops == 1 && supervisor.state() == HostState::Stopped && supervisor.reason() == StopReason::Quit &&
            !supervisor.wakeAt(),
        "quit stops the host and plans nothing");
  supervisor.ended(0, 3000);
  supervisor.wake(100000);
  check(host.starts == 1 && supervisor.state() == HostState::Stopped && supervisor.reason() == StopReason::Quit &&
            !supervisor.wakeAt(),
        "its end after quit starts nothing");

  FakeHost waiting;
  HostSupervisor restarting(waiting, kArgs);
  restarting.start(0);
  restarting.ended(1, 100);
  restarting.quit();
  restarting.wake(5100);
  check(waiting.starts == 1 && restarting.state() == HostState::Stopped && restarting.reason() == StopReason::Quit,
        "quit while it waits to restart cancels the restart");
}

void cannotStart() {
  FakeHost host;
  host.refuse = true;
  HostSupervisor supervisor(host, kArgs);
  supervisor.start(0);
  check(supervisor.state() == HostState::Stopped && supervisor.reason() == StopReason::CannotStart &&
            !supervisor.wakeAt(),
        "a host that cannot be started stops with that reason");
  host.refuse = false;
  supervisor.start(10);
  check(supervisor.state() == HostState::Starting && supervisor.reason() == StopReason::None,
        "start tries again");
  supervisor.ended(1, 20);
  host.refuse = true;
  supervisor.wake(5020);
  check(host.starts == 3 && supervisor.state() == HostState::Stopped &&
            supervisor.reason() == StopReason::CannotStart && !supervisor.wakeAt(),
        "a restart that cannot start the host stops too, with no endless retry");
}

}

int main() {
  startsAndSettles();
  backsOff();
  portBusy();
  quits();
  cannotStart();
  return awtrix::test::finish("host supervisor");
}
