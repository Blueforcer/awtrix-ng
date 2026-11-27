#include "../support.h"

#import <Foundation/Foundation.h>

#include <sys/stat.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "platform/host/app/macos/DefaultsChoice.h"
#include "platform/host/app/macos/LaunchAgentLoginItem.h"
#include "platform/host/app/macos/StartAtLogin.h"

namespace fs = std::filesystem;
using awtrix::hostapp::DefaultsChoice;
using awtrix::hostapp::LaunchAgentLoginItem;
using awtrix::hostapp::LoginChoice;
using awtrix::hostapp::LoginItem;
using awtrix::hostapp::StartAtLogin;
using awtrix::test::check;
using awtrix::test::require;

namespace {

const char kLabel[] = "de.blueforcer.awtrix.script-host";

class FakeItem : public LoginItem {
 public:
  bool enabled() const override { return on; }
  bool enable(bool wanted) override {
    ++calls;
    if (refuse) return false;
    if (!ignore) on = wanted;
    return true;
  }

  bool on = false;
  bool refuse = false;
  bool ignore = false;
  int calls = 0;
};

class FakeChoice : public LoginChoice {
 public:
  std::optional<bool> stored() const override { return value; }
  void store(bool on) override { value = on; }

  std::optional<bool> value;
};

void firstRunTurnsItOn() {
  FakeItem item;
  FakeChoice choice;
  StartAtLogin login(item, choice);
  check(login.launched() && item.on && choice.value == true, "the first run turns start at login on and keeps it");
  check(login.launched() && item.calls == 1, "a second run leaves it alone");

  FakeItem already;
  already.on = true;
  FakeChoice fresh;
  StartAtLogin again(already, fresh);
  check(again.launched() && already.calls == 0 && fresh.value == true,
        "a first run with the item already on only records the choice");
}

void theUsersChoiceStays() {
  FakeItem item;
  FakeChoice choice;
  choice.value = false;
  StartAtLogin login(item, choice);
  check(login.launched() && !item.on && item.calls == 0, "after the user turned it off, a start keeps it off");

  FakeItem settings;
  FakeChoice chosen;
  chosen.value = true;
  StartAtLogin later(settings, chosen);
  check(later.launched() && !settings.on && settings.calls == 0,
        "turned off in the system settings, a start does not turn it on again");

  check(login.toggle() && item.on && choice.value == true && login.on(), "the menu turns it on");
  check(login.toggle() && !item.on && choice.value == false && !login.on(), "and off again");
}

void refusalsChangeNothing() {
  FakeItem item;
  item.refuse = true;
  FakeChoice choice;
  StartAtLogin login(item, choice);
  check(!login.launched() && !choice.value, "a refused first run records nothing, so the next start tries again");
  item.refuse = false;
  check(login.launched() && item.on && choice.value == true, "and the next start turns it on");

  item.refuse = true;
  check(!login.toggle() && item.on && choice.value == true, "a refused toggle reports it and keeps the choice");

  FakeItem quiet;
  quiet.ignore = true;
  FakeChoice quietChoice;
  StartAtLogin pending(quiet, quietChoice);
  check(!pending.launched() && !quietChoice.value, "an item that reports success but stays off counts as refused");
  check(!pending.toggle() && !pending.on() && !quietChoice.value, "for the menu too");
}

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

NSDictionary* plist(const fs::path& path) {
  return [NSDictionary dictionaryWithContentsOfURL:[NSURL fileURLWithPath:@(path.c_str())] error:nil];
}

void launchAgent(const fs::path& root) {
  const fs::path folder = root / "Library" / "LaunchAgents";
  const std::string app = "/Applications/A&B <Test> \"Host\".app";
  LaunchAgentLoginItem item(folder.string(), kLabel, app);
  check(item.path() == (folder / (std::string(kLabel) + ".plist")).string(), "the agent is named after the label");
  check(!item.enabled(), "no agent file, not enabled");
  check(item.enable(false) && !item.enabled(), "turning off an agent that is not there succeeds");

  check(item.enable(true) && item.enabled(), "turning it on writes the agent, creating the folder");
  NSDictionary* agent = plist(item.path());
  check(agent != nil, "the agent is a valid property list");
  check([agent[@"Label"] isEqualToString:@(kLabel)], "with the label");
  NSArray* program = agent[@"ProgramArguments"];
  check(program.count == 2 && [program[0] isEqualToString:@"/usr/bin/open"] && [program[1] isEqualToString:@(app.c_str())],
        "that opens the app, with the special characters of its path intact");
  check([agent[@"RunAtLoad"] boolValue] && [agent[@"LimitLoadToSessionType"] isEqualToString:@"Aqua"],
        "at the login of a desktop session");
  struct stat st {};
  check(::stat(item.path().c_str(), &st) == 0 && (st.st_mode & 0777) == 0644,
        "readable for launchd, writable only by the user");
  check(!fs::exists(item.path() + ".partial"), "and no partial file is left");

  const std::string before = slurp(item.path());
  check(item.enable(true) && slurp(item.path()) == before, "turning it on again keeps the same agent");
  check(item.enable(false) && !item.enabled() && !fs::exists(item.path()), "turning it off removes the agent");

  const fs::path blocked = root / "blocked";
  fs::create_directories(blocked);
  fs::permissions(blocked, fs::perms::owner_read | fs::perms::owner_exec);
  LaunchAgentLoginItem denied((blocked / "LaunchAgents").string(), kLabel, app);
  check(!denied.enable(true) && !denied.enabled(), "a folder that cannot be created refuses it");
  fs::permissions(blocked, fs::perms::owner_all);

  const fs::path readOnly = root / "readonly";
  fs::create_directories(readOnly);
  LaunchAgentLoginItem locked(readOnly.string(), kLabel, app);
  fs::permissions(readOnly, fs::perms::owner_read | fs::perms::owner_exec);
  check(!locked.enable(true) && !locked.enabled() && !fs::exists(locked.path() + ".partial"),
        "a folder that cannot be written refuses it and leaves nothing behind");
  fs::permissions(readOnly, fs::perms::owner_all);
  check(locked.enable(true) && locked.enabled(), "once writable it works");
  fs::permissions(readOnly, fs::perms::owner_read | fs::perms::owner_exec);
  check(!locked.enable(false) && locked.enabled(), "an agent that cannot be removed stays on and says so");
  fs::permissions(readOnly, fs::perms::owner_all);

  const fs::path file = root / "file";
  std::ofstream(file) << "x";
  LaunchAgentLoginItem notAFolder(file.string(), kLabel, app);
  check(!notAFolder.enable(true) && !notAFolder.enabled(), "a file where the folder belongs refuses it");
}

void defaults() {
  NSString* suite = [NSString stringWithFormat:@"de.blueforcer.awtrix.test.login.%d", ::getpid()];
  NSUserDefaults* store = [[NSUserDefaults alloc] initWithSuiteName:suite];
  require(store != nil, "a defaults suite");
  DefaultsChoice choice(store, @"StartAtLogin");
  check(!choice.stored(), "nothing stored at first");
  choice.store(false);
  check(choice.stored() == false, "a stored off");
  choice.store(true);
  check(choice.stored() == true, "a stored on");
  [store setObject:@"yes" forKey:@"StartAtLogin"];
  check(!choice.stored(), "a value of the wrong kind counts as nothing stored");
  [store removePersistentDomainForName:suite];
}

}

int main() {
  @autoreleasepool {
    const fs::path root = fs::temp_directory_path() /
                          ("awtrix-macos-login-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    firstRunTurnsItOn();
    theUsersChoiceStays();
    refusalsChangeNothing();
    launchAgent(root);
    defaults();
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  return awtrix::test::finish("macOS start at login");
}
