#include "platform/linux/host/HostPersistence.h"
#include "platform/linux/host/HostStore.h"
#include "persistence/Filesystem.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace host = awtrix::host;
namespace persistence = awtrix::host::persistence;
namespace fs = std::filesystem;
using Document = persistence::Document;
std::string logOutput;
namespace awtrix {
void logf(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  logOutput += buffer;
}
}
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #expression); } while (0)

int main() {
  fs::path directory;
  int result = 0;
  try {
    directory = fs::temp_directory_path() / ("awtrix-persistence-test-" +
      std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(directory));
    host::setDataDir((directory / "data").u8string());
    CHECK(awtrix::fs::begin());
    CHECK(!persistence::hasPending());
    CHECK(persistence::save(Document::Settings, "{\"brightness\":5}"));
    CHECK(!persistence::pending(Document::Settings));

    // Force real filesystem failures. Each document is retained independently.
    const std::string secret = "private-credential-must-not-be-logged";
    const auto configPath = host::hostPath("/device.json");
    const auto orderPath = host::hostPath("/apploop.json");
    CHECK(fs::create_directory(fs::u8path(configPath)));
    CHECK(fs::create_directory(fs::u8path(orderPath)));
    CHECK(!persistence::save(Document::DeviceConfig, "{\"authPass\":\"" + secret + "\",\"revision\":1}"));
    CHECK(!persistence::save(Document::AppOrder, "[\"Time\"]"));
    CHECK(persistence::hasPending());
    CHECK(persistence::pending(Document::DeviceConfig));
    CHECK(persistence::pending(Document::AppOrder));
    CHECK(!persistence::pending(Document::Settings));
    CHECK(!persistence::flushPending());
    CHECK(logOutput.find(secret) == std::string::npos);

    // A later failed save supersedes the older intent; retries cannot roll back it.
    CHECK(!persistence::save(Document::DeviceConfig, "{\"revision\":2}"));
    CHECK(fs::remove(fs::u8path(configPath)));
    CHECK(!persistence::flushPending()); // App order still blocked.
    CHECK(!persistence::pending(Document::DeviceConfig));
    std::string actual;
    CHECK(host::readFile(configPath, actual));
    CHECK(actual == "{\"revision\":2}");
    CHECK(fs::remove(fs::u8path(orderPath)));
    CHECK(persistence::flushPending());
    CHECK(!persistence::hasPending());
    CHECK(host::readFile(orderPath, actual));
    CHECK(actual == "[\"Time\"]");

    // Quota failure preserves the durable file; the newest settings stay pending.
    const auto fill = host::hostPath("/fill.bin");
    CHECK(host::writeFile(fill, std::string(host::kFsTotalBytes - 4096, 'x')));
    const std::string newSettings = "{\"text\":\"" + std::string(8192, 's') + "\"}";
    CHECK(!persistence::save(Document::Settings, newSettings));
    CHECK(host::readFile(host::hostPath("/settings.json"), actual));
    CHECK(actual == "{\"brightness\":5}");
    CHECK(persistence::pending(Document::Settings));
    CHECK(fs::remove(fs::u8path(fill)));
    CHECK(persistence::flushPending());
    CHECK(host::readFile(host::hostPath("/settings.json"), actual));
    CHECK(actual == newSettings);

    // A changed startup root cannot receive an earlier instance's pending data.
    const auto radio = host::hostPath("/radio.json");
    CHECK(fs::create_directory(fs::u8path(radio)));
    CHECK(!persistence::save(Document::Radio, "[]"));
    const auto originalRoot = host::dataDir();
    host::setDataDir((directory / "other").u8string());
    CHECK(awtrix::fs::begin());
    CHECK(!persistence::flushPending());
    CHECK(!fs::exists(fs::u8path(host::hostPath("/radio.json"))));
    host::setDataDir(originalRoot);
    CHECK(fs::remove(fs::u8path(radio)));
    CHECK(persistence::flushPending());
    CHECK(!persistence::hasPending());
    std::puts("host persistence: failures retained, latest state retried, document isolation, quota and private logs passed");
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    result = 1;
  }
  std::error_code error;
  if (directory.is_absolute() && directory.parent_path() == fs::temp_directory_path() &&
      directory.filename().u8string().rfind("awtrix-persistence-test-", 0) == 0)
    fs::remove_all(directory, error);
  return result;
}
