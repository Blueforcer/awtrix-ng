#include "../support.h"
// The private records of script sign-ins.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "platform/linux/oauth/OAuthVault.h"

using namespace awtrix;
namespace fs = std::filesystem;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

}

int main() {
  const fs::path root = fs::temp_directory_path() / ("oauth-vault-" + std::to_string(::getpid()));
  fs::remove_all(root);
  const std::string dir = (root / "script-private").string();
  oauth::Vault vault(dir);

  oauth::Record r;
  check(!vault.load("Spotify", r), "nothing stored yet");
  r.binding = "b1";
  r.clientId = "cid";
  r.clientSecret = "sec";
  r.refreshToken = "rt";
  check(vault.save("Spotify", r), "save");
  oauth::Record back;
  check(vault.load("Spotify", back) && back.app == "Spotify" && back.binding == "b1" && back.clientId == "cid" &&
            back.clientSecret == "sec" && back.refreshToken == "rt",
        "round trip");

  struct stat st {};
  check(::stat(dir.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700, "directory is 0700");
  int files = 0;
  for (const auto& e : fs::directory_iterator(dir)) {
    ++files;
    check(e.path().filename().string().find("Spotify") == std::string::npos, "file name does not reveal the app");
    check(::stat(e.path().c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "record is 0600");
  }
  check(files == 1, "one file per app");

  check(vault.save("../evil name", r) && vault.load("../evil name", back), "any script name is a safe file name");
  check(!vault.load("Other", back), "records are per app");
  vault.keepOnly([](const oauth::Record& record) { return record.app == "Spotify"; });
  check(vault.load("Spotify", back) && !vault.load("../evil name", back), "only the records kept stay");
  check(vault.erase("Spotify") && !vault.load("Spotify", back), "erase");
  check(vault.erase("Spotify"), "erasing nothing is fine");
  check(vault.eraseAll() && !fs::exists(dir), "erase all");
  fs::remove_all(root);
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
