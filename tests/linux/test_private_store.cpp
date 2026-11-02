#include "../support.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "platform/posix/Digest.h"
#include "platform/posix/PrivateStore.h"

using awtrix::posix::PrivateStore;
using awtrix::test::check;
namespace fs = std::filesystem;

namespace {

int mode(const fs::path& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0 ? static_cast<int>(st.st_mode & 0777) : -1;
}

std::vector<std::string> names(const fs::path& dir) {
  std::vector<std::string> out;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator(dir, error)) out.push_back(entry.path().filename().string());
  std::sort(out.begin(), out.end());
  return out;
}

}

int main() {
  const fs::path root = fs::temp_directory_path() / ("private-store-" + std::to_string(::getpid()));
  fs::remove_all(root);
  const fs::path dir = root / "nested" / "store";
  PrivateStore store(dir.string(), 64);

  std::string text = "left over";
  check(!store.load("Spotify", text), "nothing stored yet");
  check(store.eraseAll(), "erasing a store that was never written is fine");
  check(store.erase("Spotify"), "erasing a missing name is fine");

  check(store.save("Spotify", "{\"app\":\"Spotify\"}"), "the first save creates the directory and its parents");
  check(mode(dir) == 0700, "the directory is 0700");
  check(store.load("Spotify", text) && text == "{\"app\":\"Spotify\"}", "load returns what was saved");
  const std::string file = awtrix::posix::sha256Hex("Spotify").substr(0, 32) + ".json";
  check(names(dir) == std::vector<std::string>{file}, "one file, named by the hash of the name");
  check(mode(dir / file) == 0600, "the file is 0600");

  check(store.save("Spotify", "second") && store.load("Spotify", text) && text == "second", "a save replaces");
  check(names(dir).size() == 1, "a replace leaves no second file");

  check(store.save("../evil name", "{\"app\":\"../evil name\"}") && store.load("../evil name", text) &&
            text == "{\"app\":\"../evil name\"}",
        "any name is a safe file name");
  check(names(dir).size() == 2, "each name has a file of its own");
  check(store.load("Spotify", text) && text == "second", "names do not share a file");

  check(!store.save("Big", std::string(65, 'x')) && !store.load("Big", text), "a text above the limit is refused");
  check(store.save("Edge", std::string(64, 'x')) && store.load("Edge", text) && text.size() == 64,
        "a text at the limit is kept");
  const std::string tooLong = awtrix::posix::sha256Hex("TooLong").substr(0, 32) + ".json";
  std::ofstream(dir / tooLong) << std::string(65, 'y');
  const std::string leftover = file + ".tmp.Ab12Cd";
  std::ofstream(dir / leftover) << "{\"app\":\"left over\"}";
  std::ofstream(dir / "planted.json") << "{\"app\":\"planted\"}";
  const std::string shortName = file.substr(1);
  std::ofstream(dir / shortName) << "{\"app\":\"short\"}";
  std::string upperName = awtrix::posix::sha256Hex("Upper").substr(0, 32);
  std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  upperName += ".json";
  std::ofstream(dir / upperName) << "{\"app\":\"upper\"}";

  const fs::path unreadable = dir / (awtrix::posix::sha256Hex("Unreadable").substr(0, 32) + ".json");
  fs::create_directory(unreadable);

  std::vector<std::string> seen;
  store.keepOnly([&seen](const std::string& content) {
    seen.push_back(content);
    return content.rfind("{\"app\":", 0) == 0;
  });
  std::sort(seen.begin(), seen.end());
  check(seen == std::vector<std::string>{"second", std::string(64, 'x'), "{\"app\":\"../evil name\"}"},
        "keepOnly hands over each readable file's content, which tells the names apart");
  check(store.load("../evil name", text) && !store.load("Spotify", text) && !store.load("Edge", text),
        "only the texts kept stay");
  check(fs::exists(unreadable), "a record that cannot be read now stays");
  fs::remove(unreadable);
  check(!fs::exists(dir / tooLong), "a file above the limit is no readable text and goes");
  check(!fs::exists(dir / leftover), "a leftover of an interrupted save goes, whatever it holds");
  check(!fs::exists(dir / "planted.json") && !fs::exists(dir / shortName) && !fs::exists(dir / upperName),
        "a name no record can have goes");

  check(store.erase("../evil name") && !store.load("../evil name", text), "erase");
  check(store.save("Spotify", "x") && store.eraseAll() && !fs::exists(dir), "eraseAll removes the directory");
  check(fs::exists(dir.parent_path()), "eraseAll keeps what lies above the store");

  fs::remove_all(root);
  return awtrix::test::finish("posix-private-store");
}
