#include "../support.h"

#include "platform/linux/host/OwnPage.h"
#include "platform/linux/host/vendor/httplib.h"

using awtrix::host::fromOwnPage;
using awtrix::host::refuseForeign;
using awtrix::test::check;

namespace {

constexpr const char* kOwn = "http://192.168.1.50";
constexpr const char* kMarker = "X-Awtrix-Secret";

httplib::Request request(const char* marker, const char* value, const char* origin) {
  httplib::Request r;
  r.headers.emplace("Host", "192.168.1.50");
  if (origin) r.headers.emplace("Origin", origin);
  if (marker) r.headers.emplace(marker, value);
  return r;
}

}

int main() {
  check(fromOwnPage(request(kMarker, "1", kOwn), kMarker), "the marker from the device's own page");
  check(fromOwnPage(request("x-awtrix-secret", "1", kOwn), kMarker), "header names ignore case");
  check(!fromOwnPage(request("X-Awtrix-OAuth", "1", kOwn), kMarker), "another feature's marker");
  check(!fromOwnPage(request(kMarker, "0", kOwn), kMarker), "a marker that is not 1");
  check(!fromOwnPage(request(nullptr, nullptr, kOwn), kMarker), "no marker");
  check(!fromOwnPage(request(kMarker, "1", "https://evil.example"), kMarker), "a foreign origin");
  check(!fromOwnPage(request(kMarker, "1", "http://192.168.1.50:8080"), kMarker), "another port");
  check(!fromOwnPage(request(kMarker, "1", nullptr), kMarker), "no origin");

  httplib::Request twoOrigins = request(kMarker, "1", kOwn);
  twoOrigins.headers.emplace("Origin", kOwn);
  check(!fromOwnPage(twoOrigins, kMarker), "two Origin headers");
  httplib::Request twoHosts = request(kMarker, "1", kOwn);
  twoHosts.headers.emplace("Host", "192.168.1.50");
  check(!fromOwnPage(twoHosts, kMarker), "two Host headers");

  httplib::Response untouched;
  check(!refuseForeign(request(kMarker, "1", kOwn), untouched, kMarker) && untouched.body.empty(),
        "the device's own page goes on, with nothing answered");
  httplib::Response refused;
  check(refuseForeign(request(kMarker, "1", "https://evil.example"), refused, kMarker) && refused.status == 403 &&
            refused.body.find("forbiddenOrigin") != std::string::npos &&
            refused.body.find("only from this device's web page") != std::string::npos,
        "anyone else gets the one refusal");
  return awtrix::test::finish("linux-own-page");
}
