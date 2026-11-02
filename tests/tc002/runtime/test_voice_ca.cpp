#include "../../support.h"
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "platform/linux/net/FileDownload.h"
#include "platform/linux/tls/TrustStore.h"
#include "platform/tc002/voice/VoiceConfig.h"

using namespace awtrix;
using namespace awtrix::tc002::voice;
constexpr auto check = test::require;
int main(int argc, char** argv) {
  if (argc != 5) return 2;
  std::string ca, wrong, leaf;
  check(posix::readText(argv[1], ca) && posix::readText(argv[2], wrong) &&
        posix::readText(argv[3], leaf), "fixture certificates");
  const std::string origin = std::string("https://localhost:") + argv[4];
  tls::setPublicAnchors(tls::TrustAnchors::fromCaPem(wrong));
  char directory[] = "/tmp/awtrix-voice-ca-XXXXXX";
  check(::mkdtemp(directory) != nullptr, "temporary configuration");
  ConfigStore config(directory);
  ConfigStore::Rejection error;
  std::string legacy;
  api::JsonWriter(legacy).beginObject().member("url", origin).member("token", "test-token")
      .member("enabled", true).member("ca", ca).endObject();
  check(posix::replaceText(std::string(directory) + "/voice.json", legacy), "old test-build settings");
  check(config.load() && config.get().token == "test-token" && config.get().url == origin,
        "legacy Voice settings retain URL and token");
  check(config.publicJson().find("caSet") == std::string::npos && config.publicJson().find("test-token") == std::string::npos,
        "no separate Voice trust or token exposed");
  check(!config.update(R"({"ca":"ignored"})", error) && error.field == "ca", "retired CA input rejected");
  check(config.update(R"({"pipeline":"other"})", error), "save other Voice setting");
  std::string saved;
  check(posix::readText(std::string(directory) + "/voice.json", saved, kMaxConfigBytes) &&
        saved.find("BEGIN CERTIFICATE") == std::string::npos, "old CA stripped on save");
  auto store = std::make_shared<tls::TrustStore>(std::string(directory) + "/trust");
  check(store->uploadCa("localhost", ca) == 200, "shared CA upload");
  auto reloaded = std::make_shared<tls::TrustStore>(std::string(directory) + "/trust");
  net::FileDownload download("/tmp", "voice-ca-test", {1024, 3000, true, nullptr});
  const auto fetch = [&](const std::string& url, const char* path,
                         std::shared_ptr<tls::TrustStore> trust, bool success, int status) {
    const auto generation = download.start(url, path, std::move(trust));
    net::FileDownload::Result result;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool done = false;
    while (!(done = download.poll(result)) && std::chrono::steady_clock::now() < end)
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    check(done && result.generation == generation && result.success() == success && result.status == status,
          "download uses only the requested trust");
    if (success) {
      std::string body;
      check(posix::readText(result.path, body) && body == "voice-test", "TTS bytes exact");
      ::unlink(result.path.c_str());
    } else check(result.path.empty(), "failed download leaves no file");
  };
  fetch(origin, "/api/tts_proxy/ok", reloaded, true, 200);
  fetch(origin, "/api/tts_proxy/default", {}, false, 0);
  fetch(std::string("https://127.0.0.1:") + argv[4], "/api/tts_proxy/wrong-name", reloaded, false, 0);
  fetch(origin, "/api/tts_proxy/redirect", reloaded, false, 302);
  check(reloaded->remove("localhost") == 200, "remove saved host trust");
  fetch(origin, "/api/tts_proxy/revoked", reloaded, false, 0);
  check(config.erase(), "factory reset clears Voice settings");
  std::filesystem::remove_all(std::string(directory) + "/trust");
  ::rmdir(directory);
  std::puts("Shared Voice trust: legacy settings, TTS persistence, name verification, redirects and revocation passed");
}
