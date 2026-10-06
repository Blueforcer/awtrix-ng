#include <unity.h>

#include "persistence/ScriptStore.h"

namespace awtrix { void logf(const char*, ...) {} }

namespace {
struct Files : awtrix::scriptfiles::DirectWrites {
  static inline std::map<std::string, std::string> disk;
  static inline bool fail = false;
  static inline int writes = 0, probes = 0;
  static bool read(const std::string& path, std::string& out) {
    const auto it = disk.find(path);
    if (it == disk.end()) return false;
    out = it->second;
    return true;
  }
  static bool readSource(const std::string& name, std::string& out) {
    return read(awtrix::scriptfiles::sourcePath(name), out);
  }
  static bool write(const std::string& path, const std::string& body) {
    ++writes;
    if (fail) return false;
    disk[path] = body;
    return true;
  }
  static void remove(const std::string& path) { disk.erase(path); }
  static std::size_t freeBytes() { ++probes; return 1024 * 1024; }
  static void forEachName(const awtrix::scriptfiles::NameVisitor& visit) {
    for (const auto& file : disk) visit(file.first.substr(file.first.rfind('/') + 1));
  }
  static bool acceptSource(const std::string&, const std::string&, std::size_t) { return true; }
  static void saveSource(const std::string& name, const std::string& body) {
    write(awtrix::scriptfiles::sourcePath(name), body);
  }
};
using Store = awtrix::ScriptStore<Files>;

void buffered_state_is_visible_and_flushed_on_its_deadline() {
  Store store;
  store.storeChanged("clock", "first");
  store.storeChanged("clock", "latest");
  std::string value;
  TEST_ASSERT_TRUE(store.readStore("clock", value));
  TEST_ASSERT_EQUAL_STRING("latest", value.c_str());
  const auto probes = Files::probes;
  for (int now = 0; now < 5000; now += 25) store.tick(now);
  TEST_ASSERT_EQUAL_INT(0, Files::writes);
  TEST_ASSERT_EQUAL_INT(probes, Files::probes);
  store.tick(5000);
  TEST_ASSERT_FALSE(store.hasPending());
  TEST_ASSERT_EQUAL_STRING("latest", Files::disk["/SCRIPTS/clock.store.json"].c_str());
}

void failed_writes_remain_readable_and_retry() {
  Store store;
  Files::fail = true;
  store.storeChanged("clock", "new state");
  store.flush();
  TEST_ASSERT_TRUE(store.hasPending());
  std::string value;
  TEST_ASSERT_TRUE(store.readStore("clock", value));
  TEST_ASSERT_EQUAL_STRING("new state", value.c_str());
  Files::fail = false;
  store.flush();
  TEST_ASSERT_FALSE(store.hasPending());
  Store reloaded;
  TEST_ASSERT_TRUE(reloaded.readStore("clock", value));
  TEST_ASSERT_EQUAL_STRING("new state", value.c_str());
}

void restore_reads_current_state_and_remove_drops_pending_writes() {
  Store store;
  store.save("clock", "source");
  Files::disk["/SCRIPTS/clock.store.json"] = "old";
  store.storeChanged("clock", "new");
  int loaded = 0;
  store.loadAll([&](const auto& name, const auto& source, const auto& state) {
    ++loaded;
    TEST_ASSERT_EQUAL_STRING("clock", name.c_str());
    TEST_ASSERT_EQUAL_STRING("source", source.c_str());
    TEST_ASSERT_EQUAL_STRING("new", state.c_str());
  });
  TEST_ASSERT_EQUAL_INT(1, loaded);
  store.remove("clock");
  store.flush();
  TEST_ASSERT_FALSE(store.hasPending());
  TEST_ASSERT_TRUE(Files::disk.empty());
}

void stored_names_cannot_address_other_files() {
  Store store;
  for (const std::string& name : {std::string(), std::string("../x"), std::string("x/y"),
                                std::string("x\\y"), std::string("x:y"), std::string("x\0y", 3),
                                std::string(65, 'x')}) {
    store.save(name, "source");
    store.storeChanged(name, "state");
  }
  store.flush();
  TEST_ASSERT_TRUE(Files::disk.empty());
  store.save(std::string(64, 'x'), "source");
  TEST_ASSERT_EQUAL_UINT(1, store.names().size());
}
}

void setUp() { Files::disk.clear(); Files::fail = false; Files::writes = Files::probes = 0; }
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(buffered_state_is_visible_and_flushed_on_its_deadline);
  RUN_TEST(failed_writes_remain_readable_and_retry);
  RUN_TEST(restore_reads_current_state_and_remove_drops_pending_writes);
  RUN_TEST(stored_names_cannot_address_other_files);
  return UNITY_END();
}
