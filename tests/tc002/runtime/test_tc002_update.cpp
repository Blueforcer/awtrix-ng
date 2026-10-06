#include "../../support.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/api/JsonWriter.h"
#include "platform/tc002/runtime/Tc002Update.h"
#include "platform/tc002/update/UpdateState.h"

using namespace awtrix;
namespace fs = std::filesystem;

namespace {
int& failures = awtrix::test::failures();
using awtrix::test::check;

void write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string manifest(const std::string& extra) {
  return "{\"schema_version\":3,\"release\":\"r\"" + extra + ",\"files\":[]}";
}
}

int main() {
  const char* base = std::getenv("TMPDIR");
  std::string buffer = std::string(base && *base ? base : "/tmp") + "/awtrix-tc002-update-XXXXXX";
  if (!::mkdtemp(&buffer[0])) return 2;
  const fs::path root = buffer;

  write(root / "rel/manifest.json", manifest(",\"counter\":1758800000"));
  check(tc002ReleaseCounter((root / "rel").string()) == 1758800000, "release counter");
  write(root / "rel2/manifest.json", manifest(",\"counter\":-1"));
  check(tc002ReleaseCounter((root / "rel2").string()) == 0, "negative counter reads as none");
  check(tc002ReleaseCounter((root / "nowhere").string()) == 0, "missing manifest reads as none");

  write(root / "rel2/manifest.json", manifest(",\"counter\":1758800000") + "trailing");
  check(tc002ReleaseCounter((root / "rel2").string()) == 0, "a malformed manifest does not supply a counter");
  fs::create_symlink(root / "rel/manifest.json", root / "rel2/link.json");
  bool linkedReadable = true;
  check(tc002AcceptedCounter((root / "rel2/link.json").string(), linkedReadable) == 0 && !linkedReadable,
        "a symlink does not supply an accepted counter");

  bool readable = false;
  check(tc002AcceptedCounter((root / "state/update-state.json").string(), readable) == 0 && readable,
        "a missing state file accepts from zero");
  tc002::update::Snapshot saved;
  saved.acceptedCounter = UINT64_MAX;
  write(root / "state/update-state.json", tc002::update::serialize(saved));
  check(tc002AcceptedCounter((root / "state/update-state.json").string(), readable) == UINT64_MAX && readable,
        "full 64-bit accepted counter from a complete state document");
  for (const char* invalid : {
           R"({"schema":2,"state":"confirmed","acceptedCounter":7})",
           R"({"schema":2,"state":"idle","acceptedCounter":7,"acceptedCounter":8,"current":null,"candidate":null,"lease":null,"failure":""})",
           R"({"schema":2,"state":"confirmed","acceptedCounter":7,"current":null,"candidate":null,"lease":null,"failure":""})"}) {
    write(root / "state/update-state.json", invalid);
    check(tc002AcceptedCounter((root / "state/update-state.json").string(), readable) == 0 && !readable,
          "partial, ambiguous and inconsistent state documents are unreadable");
  }
  write(root / "state/update-state.json", "{\"acceptedCounter\":\"5\"}");
  check(tc002AcceptedCounter((root / "state/update-state.json").string(), readable) == 0 && !readable,
        "a string counter makes the state unreadable");
  write(root / "state/update-state.json", "garbage");
  check(tc002AcceptedCounter((root / "state/update-state.json").string(), readable) == 0 && !readable,
        "garbage makes the state unreadable");

  {
    Tc002UpdateOptions options;
    options.statePath = (root / "install/state/update-state.json").string();
    options.workDirectory = (root / "work").string();
    Tc002Update update(options);
    check(update.packagePath() == (root / "work/package.awup").string(), "package path in the work directory");
    check(!update.ownsPanel(), "idle updater leaves the panel to the apps");
    check(update.poll(0, true).empty(), "nothing to hand off");
    DeviceFacts facts;
    update.addFacts(facts);
    check(facts.updateImage == Tc002Update::kImageName, "the updater names the update image");
    const auto members = [&update] {
      std::string json;
      api::JsonWriter writer(json);
      writer.beginObject();
      update.writeMembers(writer);
      writer.endObject();
      return json;
    };
    std::string json = members();
    check(json == "{\"update\":{\"state\":\"idle\",\"release\":\"\",\"error\":\"\"}}", "idle members: " + json);
    tc002::SupervisorMessage hello;
    hello.type = tc002::MessageType::Hello;
    hello.hasUpdate = true;
    hello.update = {"failed", "1.2.0-n", "the release slot was not written", 5177344};
    update.applyHello(hello);
    json = members();
    check(json == "{\"update\":{\"state\":\"failed\",\"release\":\"1.2.0-n\","
                  "\"error\":\"the release slot was not written\"}}",
          "supervisor status members: " + json);
  }

  std::error_code error;
  fs::remove_all(root, error);
  if (failures) return 1;
  std::puts("tc002 update: ok");
  return 0;
}
