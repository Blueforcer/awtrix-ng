#include "../support.h"

#include <string>

#include "platform/linux/net/PrivateHost.h"

using awtrix::net::isPrivateHost;
using awtrix::test::check;

int main() {
  for (const char* host :
       {"10.0.0.0",        "10.255.255.255",  "172.16.0.0",      "172.31.255.255", "192.168.0.0",
        "192.168.255.255", "127.0.0.1",       "127.255.255.255", "169.254.0.0",    "169.254.255.255",
        "printer.local",   "PRINTER.LOCAL",   "a.b.local",       "[::1]",          "[0:0:0:0:0:0:0:1]",
        "[fc00::]",        "[fdff::1]",       "[FD12:3456::1]",  "[fe80::]",       "[febf:ffff::1]"})
    check(isPrivateHost(host), std::string("private: ") + host);

  for (const char* host :
       {"9.255.255.255",   "11.0.0.0",        "172.15.255.255",  "172.32.0.0",
        "192.167.255.255", "192.169.0.0",     "126.255.255.255", "128.0.0.0",
        "169.253.255.255", "169.255.0.0",     "8.8.8.8",         "0.0.0.0",
        "255.255.255.255", "example.com",     "",                "local",
        ".local",          "notlocal",        "x.localhost",     "evil.local.com",
        "localhost",       "printer.local.",  "evil.com/.local", "user@evil.local",
        "192.168.1.1.evil.com", "evil.com.192.168.1.1", "192.168.1.1.", ".192.168.1.1",
        "0xC0A80001",      "0xC0.0xA8.0.1",   "3232235777",      "192.168.1",
        "192.168.1.1.1",   "010.0.0.1",       "192.168.01.1",    "0300.0250.0.1",
        "256.168.1.1",     "192.168.1.256",   "192.168.1.1000",  "+10.0.0.1",
        " 10.0.0.1",       "10.0.0.1 ",       "10.0.0.-1",       "1e1.0.0.1",
        "::1",             "fe80::1",         "[::]",            "[::2]",
        "[fbff::1]",       "[fe00::1]",       "[fe7f::1]",       "[fec0::1]",
        "[2001:db8::1]",   "[::ffff:192.168.1.1]", "[::1",       "::1]",
        "[]",              "[fe80::1%25eth0]", "[printer.local]"})
    check(!isPrivateHost(host), std::string("public: ") + host);

  return awtrix::test::finish("linux-private-host");
}
