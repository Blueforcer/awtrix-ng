#include "../support.h"
// The @oauth script header.
#include <cstdio>
#include <string>

#include "platform/linux/oauth/OAuthSpec.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

const char* kSpotify =
    "# @name Now playing\n"
    "# @oauth authorize=https://accounts.spotify.com/authorize token=https://accounts.spotify.com/api/token "
    "api=api.spotify.com scope=\"user-read-currently-playing user-read-playback-state\" pkce\n"
    "class App end\nreturn App()\n";

}

int main() {
  std::string error;
  const auto s = oauth::parseSpec(kSpotify, &error);
  check(s.has_value() && error.empty(), "spotify header parses");
  check(s && s->authorize == "https://accounts.spotify.com/authorize", "authorize url");
  check(s && s->token == "https://accounts.spotify.com/api/token", "token url");
  check(s && s->apiHosts.size() == 1 && s->apiHosts[0] == "api.spotify.com", "api host");
  check(s && s->scope == "user-read-currently-playing user-read-playback-state", "quoted scope");
  check(s && s->pkce, "pkce flag");
  check(s && s->clientAuth == oauth::ClientAuth::Basic, "basic is the default");
  check(s && s->line.rfind("authorize=", 0) == 0, "raw line kept for the binding");
  const auto upper = oauth::parseSpec(
      "# @oauth AUTHORIZE=https://a/x\vTOKEN=https://a/t API=a SCOPE=\"two words\" PKCE\n");
  check(upper && upper->scope == "two words" && upper->pkce,
        "OAuth attributes preserve case-insensitive keys and all whitespace separators");
  check(!oauth::parseSpec("# @oauth authorize= https://a/x token=https://a/t api=a\n", &error),
        "OAuth does not consume whitespace after the equals sign");

  check(!oauth::parseSpec("# @name x\nclass A end\n", &error) && error.empty(), "no tag: nothing, no error");
  check(!oauth::parseSpec("# @oauth authorize=http://a/x token=https://a/t api=a\n", &error) && !error.empty(),
        "http authorize refused");
  check(!oauth::parseSpec("# @oauth authorize=https://a/x token=https://a/t\n", &error) && !error.empty(),
        "api is required");
  check(!oauth::parseSpec("# @oauth authorize=https://a/x token=https://a/t api=a colour=red\n", &error) &&
            !error.empty(),
        "unknown attribute refused");
  check(!oauth::parseSpec("# @oauth authorize=https://a/x token=https://a/t api=a scope=\"open\n", &error) &&
            !error.empty(),
        "unterminated quote refused");
  check(!oauth::parseSpec("# @oauth authorize=https://a/x token=https://a/t api=a\n"
                          "# @oauth authorize=https://b/x token=https://b/t api=b\n",
                          &error) &&
            !error.empty(),
        "second @oauth line refused");
  check(!oauth::parseSpec("# @oauth authorize=https://a/x token=https://a/t api=a/b\n", &error), "host with path");

  const auto strava = oauth::parseSpec(
      "# @oauth authorize=https://www.strava.com/oauth/authorize token=https://www.strava.com/oauth/token "
      "api=www.strava.com scope=activity:read auth=body params=\"approval_prompt=auto\"\n",
      &error);
  check(strava && strava->clientAuth == oauth::ClientAuth::Body, "auth=body");
  check(strava && strava->params.size() == 1 && strava->params[0].first == "approval_prompt" &&
            strava->params[0].second == "auto",
        "extra authorize params");

  const auto two = oauth::parseSpec("# @oauth authorize=https://a/x token=https://a/t api=one.test,Two.test\n");
  check(two && two->apiHosts.size() == 2 && two->apiHosts[1] == "two.test", "several hosts, lower case");

  check(s && oauth::hostAllowed(*s, "https://api.spotify.com/v1/me/player"), "listed host allowed");
  check(s && oauth::hostAllowed(*s, "https://API.Spotify.com/v1/me"), "host match ignores case");
  check(s && !oauth::hostAllowed(*s, "http://api.spotify.com/v1/me"), "plain http refused");
  check(s && !oauth::hostAllowed(*s, "https://api.spotify.com.evil.io/v1"), "suffix trick refused");
  check(s && !oauth::hostAllowed(*s, "https://evil.io/?u=https://api.spotify.com"), "other host refused");
  check(s && !oauth::hostAllowed(*s, "https://api.spotify.com@evil.io/"), "user info trick refused");
  for (const auto* url : {"https://api.spotify.com:443@evil.io/", "https://user:pass@api.spotify.com/",
                          "https://@api.spotify.com/", "https://api.spotify.com\\@evil.io/",
                          "https://api.spotify.com:0/", "https://api.spotify.com:65536/"})
    check(s && !oauth::hostAllowed(*s, url), "ambiguous or credential-bearing URL refused");
  check(s && oauth::hostAllowed(*s, "https://api.spotify.com:443/v1/me"), "ordinary HTTPS port allowed");
  check(!oauth::parseSpec("# @oauth authorize=https://a:443@other/x token=https://a/t api=a\n", &error),
        "authorize URL cannot carry user information");
  check(!oauth::parseSpec("# @oauth authorize=https://a/x token=https://user@a/t api=a\n", &error),
        "token URL cannot carry user information");
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
