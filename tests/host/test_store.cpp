#include "platform/linux/host/HostStore.h"
#include "platform/linux/host/HostScriptFiles.h"
#include "persistence/Filesystem.h"
#include "persistence/VfsFile.h"
#include "media/AssetFile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace awtrix { void logf(const char*, ...) {} }
namespace fs = std::filesystem;
namespace host = awtrix::host;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #expression); } while (0)

struct Fixture {
  fs::path directory;
  Fixture() {
    directory = fs::temp_directory_path() / ("awtrix-platform-test-" +
      std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(directory));
    host::setDataDir((directory / "data").u8string());
    CHECK(awtrix::fs::begin());
  }
  ~Fixture() {
    std::error_code ec;
    // Cleanup is confined to our generated immediate child of the OS temp path.
    if (directory.is_absolute() && directory.parent_path() == fs::temp_directory_path() &&
        directory.filename().u8string().rfind("awtrix-platform-test-", 0) == 0)
      fs::remove_all(directory, ec);
  }
};

void binaryAndBounds() {
  Fixture fixture;
  const auto path = host::hostPath("/ICONS/pixels.bin");
  std::string bytes;
  for (int value = 0; value < 256; ++value) bytes += static_cast<char>(value);
  CHECK(host::writeFile(path, bytes));
  std::string actual;
  CHECK(host::readFile(path, actual));
  CHECK(actual == bytes);
  actual = "unchanged";
  CHECK(!host::readFile(path, actual, 255));
  CHECK(actual == "unchanged");
  CHECK(host::writeFile(path, ""));
  CHECK(host::readFile(path, actual, 0));
  CHECK(actual.empty());
  CHECK(!host::writeFile(host::hostPath("/missing/file"), "content"));
  CHECK(!host::readFile(host::hostPath("/ICONS"), actual));
}

void pathContainment() {
  Fixture fixture;
  const auto outside = fixture.directory / "private.txt";
  { std::ofstream file(outside); file << "private"; }
  std::string result = "untouched";
  CHECK(!host::readFile(outside.u8string(), result));
  CHECK(!host::writeFile(outside.u8string(), "changed"));
  CHECK(host::readTrustedFile(outside.u8string(), result, 7));
  CHECK(result == "private");
  CHECK(!host::readTrustedFile(outside.u8string(), result, 6));
  for (const auto& path : {"/../private.txt", "/ICONS/../device.json", "//host/share", "C:/private.txt", "/x:y", "/x\\y"})
    CHECK(host::hostPath(path).empty());
  CHECK(host::hostPath(std::string("/safe\0bad", 9)).empty());
  CHECK(!host::readFile(host::dataDir() + "/../private.txt", result));
  CHECK(!host::writeFile(host::dataDir() + "/../private.txt", "changed"));
  const auto similarPrefix = host::dataDir() + "-other/private.txt";
  CHECK(!host::writeFile(similarPrefix, "changed"));

  std::error_code ec;
  const auto link = fs::u8path(host::dataDir()) / "link";
  fs::create_directory_symlink(fixture.directory, link, ec);
  if (!ec) {
    CHECK(host::hostPath("/link/private.txt").empty());
    CHECK(!host::readFile((link / "private.txt").u8string(), result));
    CHECK(!host::writeFile((link / "new.txt").u8string(), "escape"));
    CHECK(!fs::exists(fixture.directory / "new.txt"));
  } else std::puts("symlink fixture unavailable on this OS/user; path traversal checks passed");
}

void sharedAssetFilesAndUsage() {
  Fixture fixture;
  const std::string path = "/ICONS/caf\xc3\xa9.gif";
  const std::string bytes("GIF89a\0\xff\x01", 9);
  CHECK(host::writeFile(host::hostPath(path), bytes));
  CHECK(awtrix::fs::vfsPath(path) == host::hostPath(path));
  CHECK(awtrix::fs::isFile(path));
  CHECK(awtrix::fs::fileSize(path) == static_cast<long>(bytes.size()));
  CHECK(!awtrix::fs::isFile("/ICONS"));
  CHECK(awtrix::fs::openRead("/../private") < 0);
  awtrix::media::PodBuffer<uint8_t> actual;
  CHECK(awtrix::media::readAsset(path, actual));
  CHECK(std::string(reinterpret_cast<char*>(actual.data()), actual.size()) == bytes);
  uint8_t slice[4];
  std::size_t count = 0, size = 0;
  CHECK(awtrix::media::readAssetRange(path, 6, slice, sizeof(slice), count, size));
  CHECK(count == 3 && size == bytes.size() && slice[0] == 0 && slice[1] == 255 && slice[2] == 1);
  CHECK(!awtrix::media::readAssetRange(path, bytes.size() + 1, slice, sizeof(slice), count, size));
  std::size_t total = 0, used = 0;
  CHECK(awtrix::fs::usage(total, used));
  CHECK(total == host::kFsTotalBytes && used == bytes.size());
}

void quotaAndFailedReplacement() {
  Fixture fixture;
  const auto settings = host::hostPath("/device.json");
  CHECK(host::writeFile(settings, "old"));
  CHECK(!host::writeFile(settings, std::string(host::kFsTotalBytes + 1, 'x')));
  std::string actual;
  CHECK(host::readFile(settings, actual));
  CHECK(actual == "old");
  const auto filled = host::hostPath("/full.bin");
  CHECK(host::writeFile(filled, std::string(host::kFsTotalBytes - 3, 'f')));
  CHECK(!host::writeFile(settings, "larger"));
  CHECK(host::readFile(settings, actual) && actual == "old");
  CHECK(host::writeFile(settings, "new"));
  CHECK(host::writeFile(filled, "small"));
  CHECK(host::writeFile(settings, "larger"));
  CHECK(host::readFile(settings, actual) && actual == "larger");
  for (const auto& item : fs::directory_iterator(fs::u8path(host::dataDir())))
    CHECK(item.path().filename().u8string().rfind(".awtrix-tmp-", 0) != 0);
}

void uploadsLeaveTheReserveFree() {
  Fixture fixture;
  const auto icon = host::hostPath("/ICONS/reserve.gif");
  const auto settings = host::hostPath("/settings.json");
  CHECK(host::storageCapacity(0) == host::kFsTotalBytes);
  std::error_code ec;
  const auto space = fs::space(fs::u8path(host::dataDir()), ec);
  CHECK(!ec);
  host::setUploadReserve(space.available + 64u * 1024u * 1024u);
  CHECK(!host::writeUpload(icon, "GIF89a"));
  CHECK(!fs::exists(fs::u8path(icon)));
  CHECK(host::writeFile(settings, "{\"brightness\":40}"));
  CHECK(host::storageCapacity(17) == 17);
  host::setUploadReserve(1);
  CHECK(host::writeUpload(icon, "GIF89a"));
  CHECK(!host::writeUpload(icon, std::string(host::kFsTotalBytes + 1, 'x')));
  const auto capacity = host::storageCapacity(23);
  CHECK(capacity > 23 && capacity <= host::kFsTotalBytes);
  host::setUploadReserve(0);
  CHECK(host::storageCapacity(23) == host::kFsTotalBytes);
}

void readersNeverSeePartialReplacement() {
  Fixture fixture;
  const auto path = host::hostPath("/state.json");
  const std::string first(8192, 'a'), second(8192, 'b');
  CHECK(host::writeFile(path, first));
  std::atomic<bool> finished{false}, partial{false};
  std::thread reader([&] {
    while (!finished) {
      std::string value;
      if (host::readFile(path, value) && value != first && value != second) partial = true;
    }
  });
  int succeeded = 0;
  for (int count = 0; count < 40; ++count)
    if (host::writeFile(path, count % 2 ? first : second)) ++succeeded;
  finished = true;
  reader.join();
  CHECK(succeeded > 0);
  CHECK(!partial);
}

void scriptStateRetriesAfterWriteFailure() {
  Fixture fixture;
  awtrix::ScriptStore<host::ScriptFiles> store;
  store.storeChanged("clock", "{\"count\":1}");
  const auto path = fs::u8path(host::hostPath("/SCRIPTS/clock.store.json"));
  CHECK(fs::create_directory(path)); // A real OS error, not a mocked writer.
  store.flush();
  CHECK(store.hasPending());
  std::string value;
  CHECK(store.readStore("clock", value));
  CHECK(value == "{\"count\":1}");
  CHECK(fs::remove(path));
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(path.u8string(), value));
  CHECK(value == "{\"count\":1}");
  awtrix::ScriptStore<host::ScriptFiles> reloaded;
  CHECK(reloaded.readStore("clock", value));
  CHECK(value == "{\"count\":1}");
  store.storeChanged("../outside", "bad");
  store.storeChanged("x:y", "bad");
  store.flush();
  CHECK(!fs::exists(fixture.directory / "outside.store.json"));

  const auto source = fs::u8path(host::hostPath("/SCRIPTS/clock.ax"));
  CHECK(fs::create_directory(source));
  store.save("clock", "def tick() return 1 end");
  CHECK(store.hasPending());
  CHECK(store.readSource("clock", value));
  CHECK(value == "def tick() return 1 end");
  CHECK(fs::remove(source));
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(source.u8string(), value));
  CHECK(value == "def tick() return 1 end");
}

void scriptQuotaFailureKeepsLatestValues() {
  Fixture fixture;
  awtrix::ScriptStore<host::ScriptFiles> store;
  const auto fill = host::hostPath("/full.bin");
  CHECK(host::writeFile(fill, std::string(host::kFsTotalBytes, 'x')));
  store.save("clock", "def tick() return 1 end");
  store.storeChanged("clock", "{\"count\":1}");
  CHECK(store.hasPending());
  store.save("clock", "def tick() return 2 end");
  store.storeChanged("clock", "{\"count\":2}");
  store.flush();
  CHECK(store.hasPending());
  std::string value;
  CHECK(store.readSource("clock", value) && value == "def tick() return 2 end");
  CHECK(store.readStore("clock", value) && value == "{\"count\":2}");
  CHECK(fs::remove(fs::u8path(fill)));
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(host::hostPath("/SCRIPTS/clock.ax"), value));
  CHECK(value == "def tick() return 2 end");
  CHECK(host::readFile(host::hostPath("/SCRIPTS/clock.store.json"), value));
  CHECK(value == "{\"count\":2}");
}

void scriptSourcesLeaveTheReserveFree() {
  Fixture fixture;
  std::error_code ec;
  const auto space = fs::space(fs::u8path(host::dataDir()), ec);
  CHECK(!ec);
  awtrix::ScriptStore<host::ScriptFiles> store;
  const auto source = host::hostPath("/SCRIPTS/clock.ax");
  host::setUploadReserve(space.available + 64u * 1024u * 1024u);
  store.save("clock", "def tick() return 1 end");
  CHECK(store.hasPending());
  CHECK(!fs::exists(fs::u8path(source)));
  std::string value;
  CHECK(store.readSource("clock", value) && value == "def tick() return 1 end");
  store.storeChanged("clock", "{\"count\":1}");
  store.flush();
  CHECK(host::readFile(host::hostPath("/SCRIPTS/clock.store.json"), value) && value == "{\"count\":1}");
  CHECK(store.hasPending());
  CHECK(!fs::exists(fs::u8path(source)));
  host::setUploadReserve(0);
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(source, value) && value == "def tick() return 1 end");
}

void scriptStateLeavesPartOfTheReserveFree() {
  Fixture fixture;
  std::error_code ec;
  const auto space = fs::space(fs::u8path(host::dataDir()), ec);
  CHECK(!ec);
  awtrix::ScriptStore<host::ScriptFiles> store;
  const auto state = host::hostPath("/SCRIPTS/clock.store.json");
  host::setUploadReserve(4 * (space.available + 64u * 1024u * 1024u));
  store.storeChanged("clock", "{\"count\":1}");
  store.flush();
  CHECK(store.hasPending());
  CHECK(!fs::exists(fs::u8path(state)));
  std::string value;
  CHECK(store.readStore("clock", value) && value == "{\"count\":1}");
  host::setUploadReserve(0);
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(state, value) && value == "{\"count\":1}");
}

void scriptStateIsLimitedWithAReserve() {
  Fixture fixture;
  constexpr std::size_t limit = 64 * 1024;
  awtrix::ScriptStore<host::ScriptFiles> store;
  const auto state = host::hostPath("/SCRIPTS/history.store.json");
  const std::string fits(limit, 'a'), over(limit + 1, 'b');
  std::string value;
  host::setUploadReserve(1);
  store.storeChanged("history", fits);
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(state, value) && value == fits);
  store.storeChanged("history", over);
  CHECK(!store.hasPending());
  CHECK(store.readStore("history", value) && value == fits);
  store.flush();
  CHECK(host::readFile(state, value) && value == fits);
  host::setUploadReserve(0);
  store.storeChanged("history", over);
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(state, value) && value == over);
}

void failedScriptStateWaitsForRoom() {
  Fixture fixture;
  awtrix::ScriptStore<host::ScriptFiles> store;
  std::string value;
  const auto fill = host::hostPath("/full.bin");
  CHECK(host::writeFile(fill, std::string(host::kFsTotalBytes - 256 * 1024, 'f')));
  host::setUploadReserve(1);

  const auto clock = fs::u8path(host::hostPath("/SCRIPTS/clock.store.json"));
  CHECK(fs::create_directory(clock));
  store.storeChanged("clock", std::string(1024, 'a'));
  store.flush();
  CHECK(store.hasPending());
  CHECK(fs::remove(clock));
  store.flush();
  CHECK(store.hasPending());
  CHECK(!fs::exists(clock));
  store.storeChanged("clock", std::string(2048, 'b'));
  store.flush();
  CHECK(!fs::exists(clock));
  store.storeChanged("clock", std::string(512, 'c'));
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(clock.u8string(), value) && value == std::string(512, 'c'));

  const auto radio = fs::u8path(host::hostPath("/SCRIPTS/radio.store.json"));
  CHECK(fs::create_directory(radio));
  store.storeChanged("radio", std::string(1024, 'r'));
  store.flush();
  CHECK(fs::remove(radio));
  store.flush();
  CHECK(store.hasPending());
  CHECK(!fs::exists(radio));
  CHECK(host::writeFile(fill, std::string(host::kFsTotalBytes - 512 * 1024, 'f')));
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(radio.u8string(), value) && value == std::string(1024, 'r'));
  host::setUploadReserve(0);
}

void scriptPendingPayloadBoundIsObservable() {
  Fixture fixture;
  awtrix::ScriptStore<host::ScriptFiles> store;
  std::string value;
  store.save("huge", std::string(host::kFsTotalBytes + 1, 'x'));
  CHECK(store.hasPending());
  CHECK(!store.readSource("huge", value));
  store.flush();
  CHECK(store.hasPending()); // No retained payload must not masquerade as clean.
  store.save("huge", "valid replacement");
  CHECK(!store.hasPending());

  // Combined source and store payloads share one budget. The source fits that
  // memory budget but stays pending because the filesystem margin is reserved.
  store.save("clock", std::string(host::kFsTotalBytes - 16, 's'));
  store.storeChanged("clock", std::string(32, 't'));
  CHECK(store.hasPending());
  CHECK(!store.readStore("clock", value));
  store.save("clock", "small replacement");
  store.flush();
  CHECK(store.hasPending()); // The rejected state is still explicitly unresolved.
  store.storeChanged("clock", "{\"resolved\":true}");
  store.flush();
  CHECK(!store.hasPending());
  CHECK(host::readFile(host::hostPath("/SCRIPTS/clock.store.json"), value));
  CHECK(value == "{\"resolved\":true}");

  store.storeChanged("clock", std::string(host::kFsTotalBytes + 1, 'x'));
  store.flush();
  CHECK(store.hasPending());
  store.remove("clock");
  CHECK(!store.hasPending());
}

// A script's sounds outlive source saves and removal; DELETE .../sounds removes them.
void scriptSoundsOutliveTheirScript() {
  Fixture fixture;
  awtrix::ScriptStore<host::ScriptFiles> store;
  store.save("racer", "def draw() end");
  store.save("racer2", "def draw() end");
  const auto sounds = fs::u8path(host::hostPath("/SCRIPTS/racer"));
  const auto neighbour = fs::u8path(host::hostPath("/SCRIPTS/racer2"));
  const auto shared = fs::u8path(host::hostPath("/MP3"));
  CHECK(fs::create_directory(sounds));
  CHECK(fs::create_directory(neighbour));
  CHECK(fs::create_directory(shared));
  CHECK(host::writeFile(host::hostPath("/SCRIPTS/racer/boost.mp3"), "ID3"));
  CHECK(host::writeFile(host::hostPath("/SCRIPTS/racer/lap.mp3"), "ID3"));
  CHECK(host::writeFile(host::hostPath("/SCRIPTS/racer2/boost.mp3"), "ID3"));
  CHECK(host::writeFile(host::hostPath("/MP3/boost.mp3"), "ID3"));

  // A sound folder is not a script of its own.
  auto names = store.names();
  std::sort(names.begin(), names.end());
  CHECK(names == std::vector<std::string>({"racer", "racer2"}));

  store.save("racer", "def draw() return 2 end");
  CHECK(fs::exists(sounds / "boost.mp3"));

  store.remove("racer");
  CHECK(!fs::exists(fs::u8path(host::hostPath("/SCRIPTS/racer.ax"))));
  CHECK(fs::exists(sounds / "boost.mp3") && fs::exists(sounds / "lap.mp3"));
  CHECK(fs::exists(neighbour / "boost.mp3"));
  CHECK(fs::exists(shared / "boost.mp3"));
  names = store.names();
  CHECK(names == std::vector<std::string>({"racer2"}));

  // A name no folder can have reaches nothing, the data directory least of all.
  for (const char* name : {"", ".", "..", "a.b", "racer2/..", "/"}) store.remove(name);
  CHECK(fs::exists(neighbour / "boost.mp3"));
  CHECK(fs::exists(shared / "boost.mp3"));
  CHECK(fs::exists(fs::u8path(host::hostPath("/SCRIPTS/racer2.ax"))));
}

int main() {
  try {
    binaryAndBounds();
    pathContainment();
    sharedAssetFilesAndUsage();
    quotaAndFailedReplacement();
    uploadsLeaveTheReserveFree();
    readersNeverSeePartialReplacement();
    scriptStateRetriesAfterWriteFailure();
    scriptQuotaFailureKeepsLatestValues();
    scriptSourcesLeaveTheReserveFree();
    scriptStateLeavesPartOfTheReserveFree();
    scriptStateIsLimitedWithAReserve();
    failedScriptStateWaitsForRoom();
    scriptPendingPayloadBoundIsObservable();
    scriptSoundsOutliveTheirScript();
    std::puts("host storage: 14 scenarios passed");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
