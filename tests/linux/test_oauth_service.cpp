#include "../support.h"
// Sign-in state, token refresh and requests of the script sign-in, against a fake provider.
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/linux/oauth/OAuthFlow.h"
#include "platform/linux/oauth/OAuthService.h"
#include "OAuthTestSupport.h"

using namespace awtrix;
namespace fs = std::filesystem;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

const std::string kSource =
    "# @oauth authorize=https://auth.test/authorize token=https://auth.test/token api=api.test pkce\n"
    "class App end\nreturn App()\n";

struct Fake {
  std::mutex m;
  std::vector<oauth::HttpCall> calls;
  int apiStatus = 200;
  bool apiRefuses = false;
  int refreshes = 0;
  int tokenStatus = 400;
  std::string tokenAnswer;
  oauth::HttpReply reply(const oauth::HttpCall& c) {
    std::lock_guard<std::mutex> lock(m);
    calls.push_back(c);
    if (c.url == "https://auth.test/token") {
      if (c.body.find("grant_type=refresh_token") != std::string::npos) ++refreshes;
      if (!tokenAnswer.empty()) return {tokenStatus, tokenAnswer};
      return {200, R"({"access_token":"AT)" + std::to_string(calls.size()) +
                       R"(","token_type":"Bearer","expires_in":3600,"refresh_token":"RT2"})"};
    }
    const int status = apiRefuses ? 401 : apiStatus;
    apiStatus = 200;
    return {status, "{\"bearer\":\"" + oauth_test::headerOf(c, "Authorization") + "\"}"};
  }
  std::size_t count() {
    std::lock_guard<std::mutex> lock(m);
    return calls.size();
  }
};

bool waitFor(oauth::Service& s, oauth::Result& out) {
  for (int i = 0; i < 400; ++i) {
    if (s.pop(out)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

bool waitState(oauth::Service& s, const char* state) {
  for (int i = 0; i < 400; ++i) {
    if (s.status("Spot").state == state) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

std::string stateOf(const std::string& url) {
  const std::size_t at = url.find("state=") + 6;
  return url.substr(at, url.find('&', at) - at);
}

}

int main() {
  const fs::path root = fs::temp_directory_path() / ("oauth-service-" + std::to_string(::getpid()));
  fs::remove_all(root);
  oauth::Vault vault((root / "script-private").string());
  Fake fake;
  std::mutex sourceMutex;
  std::map<std::string, std::string> sources = {{"Spot", kSource}};
  std::atomic<long long> now{1000};
  oauth::Service service(
      vault, [&](const oauth::HttpCall& c) { return fake.reply(c); },
      [&](const std::string& app) {
        std::lock_guard<std::mutex> lock(sourceMutex);
        const auto it = sources.find(app);
        return it == sources.end() ? std::string() : it->second;
      },
      [&] { return now.load(); });
  service.begin();

  std::string error, url;
  check(service.status("Spot").declared && service.status("Spot").state == "signedOut", "starts signed out");
  check(!service.status("Nobody").declared, "a script without @oauth is not declared");
  check(!service.start("Spot", "http://192.168.1.50", url, error) && error == "client id missing", "needs a client id");
  check(service.saveClient("Spot", std::string("cid"), std::nullopt, false, error), "client id saved");
  check(service.start("Spot", "http://192.168.1.50", url, error), "start");
  check(url.rfind("https://auth.test/authorize?response_type=code&client_id=cid", 0) == 0, "authorize url");
  const std::string state = stateOf(url);
  check(oauth_test::stateMember(state, "r") == "http://192.168.1.50" && oauth_test::stateMember(state, "a") == "Spot",
        "state names the way back");
  check(!service.submitCode("Spot", "c0de", "wrong", error) && error == "sign-in expired or unknown", "state checked");
  check(service.submitCode("Spot", "c0de", state, error), "code accepted");
  check(waitState(service, "signedIn"), "signed in after the exchange");
  check(fake.count() == 1 && fake.calls[0].body.find("code_verifier=") != std::string::npos, "verifier sent");
  check(!service.submitCode("Spot", "c0de", state, error), "a state is used once");
  check(service.ready("Spot"), "ready");

  oauth::Result r;
  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "request queued");
  check(waitFor(service, r) && r.app == "Spot" && r.status == 200 && r.body.find("Bearer AT1") != std::string::npos,
        "bearer from the exchange is used");
  check(service.request("Spot", "GET", "https://evil.test/me", "", {}, 4096) == 0, "unlisted host refused");
  const auto callsBeforeBadAuthority = fake.count();
  check(service.request("Spot", "GET", "https://api.test:443@evil.test/me", "", {}, 4096) == 0,
        "userinfo port trick refused before queueing");
  check(service.request("Spot", "GET", "https://@api.test/me", "", {}, 4096) == 0,
        "empty userinfo refused before queueing");
  check(fake.count() == callsBeforeBadAuthority, "no provider call or bearer token for refused authority");
  check(service.request("Spot", "GET", "https://api.test/me", "", {{"Authorization", "x"}}, 4096) == 0,
        "a script cannot set Authorization");
  check(service.request("Spot", "TRACE", "https://api.test/me", "", {}, 4096) == 0, "unknown method refused");

  check(service.request("Spot", "POST", "https://api.test/me", "{}", {}, 4096) != 0, "post queued");
  check(waitFor(service, r) &&
            oauth_test::headerOf(fake.calls.back(), "Content-Type") == "application/json",
        "a body without a type is sent as JSON");

  fake.apiStatus = 401;
  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "request queued");
  check(waitFor(service, r) && r.status == 200 && fake.refreshes == 1, "401 refreshes once and retries");

  now += 3600 * 1000;
  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "queued");
  check(waitFor(service, r) && fake.refreshes == 2, "an expired access token is refreshed first");

  oauth::Record rec;
  check(vault.load("Spot", rec) && rec.refreshToken == "RT2" && rec.accessToken.empty(), "rotated refresh token kept");
  check(rec.clientSecret.empty() && rec.clientId == "cid", "public client keeps no secret");

  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "queued before forget");
  service.forget("Spot");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(!service.pop(r), "results of a forgotten run are dropped");

  now += 3600 * 1000;
  fake.tokenStatus = 503;
  fake.tokenAnswer = "<html>busy</html>";
  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "queued");
  check(waitFor(service, r) && r.status == 0 && r.body.empty(), "a failed renewal reports no connection");
  check(service.ready("Spot") && service.status("Spot").state == "signedIn", "and keeps the sign-in");

  fake.tokenStatus = 400;
  fake.tokenAnswer = R"({"error":"invalid_grant"})";
  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "queued");
  check(waitFor(service, r) && r.status == 401 && r.body.empty(), "lost sign-in reports 401");
  check(service.status("Spot").state == "error" && service.status("Spot").error == "invalid_grant" &&
            !service.ready("Spot"),
        "invalid_grant ends the sign-in");
  fake.tokenAnswer.clear();

  check(service.saveClient("Spot", std::nullopt, std::string("s3cret"), false, error), "secret saved");
  check(service.status("Spot").clientSecretSet && service.status("Spot").clientId == "cid", "secret is write-only");
  check(service.saveClient("Spot", std::nullopt, std::nullopt, true, error) && !service.status("Spot").clientSecretSet,
        "secret cleared");

  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources["Spot"] =
        "# @oauth authorize=https://auth.test/authorize token=https://auth.test/token api=api.test,evil.test\n";
  }
  service.forget("Spot");
  check(service.status("Spot").state == "signedOut" && service.status("Spot").clientId.empty() &&
            !vault.load("Spot", rec),
        "a changed @oauth line signs out and forgets the client");

  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources["Spot"] = kSource;
  }
  service.sourceSaved("Spot");
  check(service.saveClient("Spot", std::string("cid"), std::nullopt, false, error), "saved again");
  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources["Spot"] = "class App end\n";
  }
  check(service.status("Spot").declared, "the line read before the new source was saved still counts");
  service.sourceSaved("Spot");
  check(!service.status("Spot").declared && !vault.load("Spot", rec), "a saved source without @oauth signs out");

  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources["Spot"] = kSource;
  }
  service.sourceSaved("Spot");
  check(service.saveClient("Spot", std::string("cid"), std::nullopt, false, error), "saved once more");
  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources.erase("Spot");
  }
  service.forget("Spot");
  check(!service.status("Spot").declared && vault.load("Spot", rec), "reading the state never erases a record");
  service.sourceRemoved("Spot");
  check(!vault.load("Spot", rec), "a removed script's record is erased at once");

  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources["Spot"] = kSource;
  }
  const auto spec = oauth::parseSpec(kSource);
  oauth::Record longLived;
  longLived.binding = oauth::sha256Hex("Spot\n" + spec->line);
  longLived.clientId = "cid";
  longLived.accessToken = "LONG";
  vault.save("Spot", longLived);
  service.sourceSaved("Spot");
  check(service.ready("Spot"), "a token without refresh token counts as signed in");
  fake.apiRefuses = true;
  check(service.request("Spot", "GET", "https://api.test/me", "", {}, 4096) != 0, "queued");
  check(waitFor(service, r) && r.status == 401 && r.body.empty(), "a refused long-lived token reports 401");
  check(!service.ready("Spot") && service.status("Spot").state == "error" &&
            service.status("Spot").error == "invalid_token",
        "and ends the sign-in");
  fake.apiRefuses = false;

  vault.save("Spot", longLived);
  oauth::Record stale = longLived;
  stale.binding = "old line";
  vault.save("Stale", stale);
  vault.save("Gone", longLived);
  {
    std::lock_guard<std::mutex> lock(sourceMutex);
    sources["Stale"] = kSource;
  }
  service.dropStale();
  check(vault.load("Spot", rec) && !vault.load("Stale", rec) && !vault.load("Gone", rec),
        "at start only records that still match their script stay");

  service.stop();
  fs::remove_all(root);
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
