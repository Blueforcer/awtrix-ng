#include "../../../support.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/wifi/CredentialStore.h"
#include "platform/tc002/daemon/wifi/SupplicantConfig.h"
#include "platform/tc002/daemon/wifi/WifiCrypto.h"
#include "platform/tc002/daemon/wifi/WifiSystem.h"
#include "platform/tc002/daemon/wifi/WpaControl.h"

using namespace awtrix::tc002d::wifi;

namespace {

int& failures = awtrix::test::failures();
unsigned checks = 0;

using awtrix::test::check;

template <std::size_t N>
std::string hex(const std::array<uint8_t, N>& bytes) { return awtrix::posix::hexBytes(bytes.data(), bytes.size()); }

std::string hexOf(const uint8_t* bytes, std::size_t size) { return awtrix::posix::hexBytes(bytes, size); }

std::string readText(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

void writeText(const std::string& path, const std::string& text, mode_t mode) {
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd >= 0) {
    if (write(fd, text.data(), text.size()) != static_cast<ssize_t>(text.size())) std::perror("write");
    fchmod(fd, mode);
    close(fd);
  }
}

void testSha1AndHmac() {
  check(hex(sha1("", 0)) == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1 empty");
  check(hex(sha1("abc", 3)) == "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1 abc");
  const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  check(hex(sha1(two, std::strlen(two))) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "sha1 448-bit");
  Sha1 million;
  const std::string chunk(1000, 'a');
  for (int i = 0; i < 1000; ++i) million.update(chunk.data(), chunk.size());
  check(hex(million.finish()) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "sha1 million a");
  const std::string key(20, '\x0b');
  check(hex(hmacSha1(key.data(), key.size(), "Hi There", 8)) == "b617318655057264e28bc0b6fb378c8ef146be00",
        "hmac rfc2202 case 1");
  check(hex(hmacSha1("Jefe", 4, "what do ya want for nothing?", 28)) ==
            "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", "hmac rfc2202 case 2");
  const std::string longKey(80, '\xaa');
  const char* longData = "Test Using Larger Than Block-Size Key - Hash Key First";
  check(hex(hmacSha1(longKey.data(), longKey.size(), longData, std::strlen(longData))) ==
            "aa4ae5e15272d00e95705637ce8a3b55ed402112", "hmac rfc2202 case 6 (long key)");
}

void testPbkdf2() {
  struct Vector { const char* password; std::size_t passwordSize; const char* salt; std::size_t saltSize;
                  uint32_t iterations; std::size_t length; const char* expected; };
  const Vector vectors[] = {
      {"password", 8, "salt", 4, 1, 20, "0c60c80f961f0e71f3a9b524af6012062fe037a6"},
      {"password", 8, "salt", 4, 2, 20, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"},
      {"password", 8, "salt", 4, 4096, 20, "4b007901b765489abead49d926f721d065a429c1"},
      {"passwordPASSWORDpassword", 24, "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096, 25,
       "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038"},
      {"pass\0word", 9, "sa\0lt", 5, 4096, 16, "56fa6aa75548099dcc37d7f03425e0c3"},
  };
  for (const auto& v : vectors) {
    uint8_t out[32] = {};
    pbkdf2HmacSha1(std::string_view(v.password, v.passwordSize), v.salt, v.saltSize, v.iterations, out, v.length);
    check(hexOf(out, v.length) == v.expected, "pbkdf2 rfc6070 vector");
  }
  check(hex(deriveWpaPsk("password", "IEEE")) ==
            "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", "802.11i vector 1");
  check(hex(deriveWpaPsk("ThisIsAPassword", "ThisIsASSID")) ==
            "0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af", "802.11i vector 2");
  check(hex(deriveWpaPsk(std::string(32, 'a'), std::string(32, 'Z'))) ==
            "becb93866bb8c3832cb777c2f559807c8c59afcb6eae734885001300a981cc62", "802.11i vector 3");
}

void testProfiles() {
  WifiProfile profile;
  check(makeProfile("IEEE", "password", profile) && !profile.open &&
            hex(profile.psk) == "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e",
        "passphrase derives the PSK");
  check(makeProfile("Open", "", profile) && profile.open, "empty password means open network");
  const std::string rawPsk = "00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff";
  check(makeProfile("Raw", rawPsk, profile) && !profile.open &&
            hex(profile.psk) == "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
        "64 hex digits are taken as the PSK itself");
  WifiProfile untouched;
  untouched.ssid = "keep";
  check(!makeProfile("", "password", untouched) && untouched.ssid == "keep", "empty ssid rejected");
  check(!makeProfile(std::string(33, 's'), "password", untouched), "33-byte ssid rejected");
  check(makeProfile(std::string(32, 's'), "password", profile), "32-byte ssid accepted");
  check(!makeProfile("x", "short", untouched), "7-character passphrase rejected");
  check(!makeProfile("x", std::string(64, 'g'), untouched), "64 non-hex characters rejected");
  check(!makeProfile("x", std::string(65, 'a'), untouched), "65 characters rejected");
  check(!makeProfile("x", "line\nbreak", untouched), "control character rejected");
  check(makeProfile("x", std::string(63, '~'), profile), "63-character passphrase accepted");
  check(makeProfile("Caf\xc3\xa9", "p\xc3\xa4ssw\xc3\xb6rd", profile), "UTF-8 passphrase accepted");
}

void testRecordCodec() {
  WifiProfile profile;
  makeProfile("Home \"Net\"\n", "correct horse", profile);
  const std::string record = CredentialStore::encode(profile);
  WifiProfile decoded;
  check(CredentialStore::decode(record, decoded) && decoded.ssid == profile.ssid && !decoded.open &&
            decoded.psk == profile.psk, "record round trip");
  check(record.find("correct horse") == std::string::npos, "record never holds the passphrase");
  for (std::size_t i = 0; i < record.size(); ++i) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      std::string tampered = record;
      tampered[i] = static_cast<char>(tampered[i] ^ (1u << bit));
      WifiProfile ignored;
      if (CredentialStore::decode(tampered, ignored)) {
        check(false, "every flipped bit is rejected");
        return;
      }
    }
  }
  check(true, "every flipped bit is rejected");
  for (std::size_t n = 0; n < record.size(); ++n) {
    WifiProfile ignored;
    if (CredentialStore::decode(record.substr(0, n), ignored)) { check(false, "truncation rejected"); return; }
  }
  check(!CredentialStore::decode(record + '\0', decoded), "trailing byte rejected");
  WifiProfile open;
  makeProfile("Cafe", "", open);
  check(CredentialStore::decode(CredentialStore::encode(open), decoded) && decoded.open && decoded.ssid == "Cafe",
        "open record round trip");
}

void testStore(const std::string& root) {
  const std::string directory = root + "/state/network";
  CredentialStore store(directory);
  WifiProfile loaded;
  check(store.load(loaded) == StoreStatus::Missing, "missing store");
  WifiProfile profile;
  makeProfile("Home", "correct horse", profile);
  int error = 0;
  check(store.save(profile, error), "save creates parents");
  struct stat info {};
  check(stat(directory.c_str(), &info) == 0 && (info.st_mode & 07777) == 0700, "directory is 0700");
  const std::string file = directory + "/" + CredentialStore::kFileName;
  check(stat(file.c_str(), &info) == 0 && (info.st_mode & 07777) == 0600, "file is 0600");
  check(access((directory + "/" + CredentialStore::kTemporaryName).c_str(), F_OK) != 0, "no temporary left behind");
  check(store.load(loaded) == StoreStatus::Loaded && loaded.ssid == "Home" && loaded.psk == profile.psk, "load");
  check(readText(file).find("correct horse") == std::string::npos, "passphrase never persisted");

  writeText(directory + "/" + CredentialStore::kTemporaryName, "stale", 0600);
  WifiProfile second;
  makeProfile("Second", "another secret", second);
  check(store.save(second, error), "save replaces a stale temporary from a crash");
  check(store.load(loaded) == StoreStatus::Loaded && loaded.ssid == "Second", "replacement is atomic and complete");
  check(access((directory + "/" + CredentialStore::kTemporaryName).c_str(), F_OK) != 0, "stale temporary removed");

  chmod(file.c_str(), 0644);
  check(store.load(loaded) == StoreStatus::Unsafe, "wrong file mode refused");
  chmod(file.c_str(), 0600);

  std::string bytes = readText(file);
  bytes[bytes.size() / 2] ^= 1;
  writeText(file, bytes, 0600);
  check(store.load(loaded) == StoreStatus::Invalid, "corrupted record refused");

  unlink(file.c_str());
  writeText(root + "/elsewhere", CredentialStore::encode(profile), 0600);
  if (symlink((root + "/elsewhere").c_str(), file.c_str()) == 0)
    check(store.load(loaded) == StoreStatus::Unsafe, "symlinked record refused");
  check(store.save(profile, error) && store.load(loaded) == StoreStatus::Loaded, "save replaces the symlink itself");
  check(readText(root + "/elsewhere") == CredentialStore::encode(profile), "symlink target untouched");

  const std::string hardlink = root + "/hardlink";
  if (link(file.c_str(), hardlink.c_str()) == 0) {
    check(store.load(loaded) == StoreStatus::Unsafe, "hard-linked record refused");
    unlink(hardlink.c_str());
  }

  chmod(directory.c_str(), 0755);
  check(store.load(loaded) == StoreStatus::Unsafe, "group-readable directory refused");
  check(store.save(profile, error) && stat(directory.c_str(), &info) == 0 && (info.st_mode & 07777) == 0700,
        "save repairs the directory mode");

  const std::string linkedDirectory = root + "/linked";
  if (symlink(directory.c_str(), linkedDirectory.c_str()) == 0) {
    CredentialStore linked(linkedDirectory);
    check(linked.load(loaded) == StoreStatus::Unsafe, "symlinked directory refused");
    check(!linked.save(profile, error), "symlinked directory not written");
  }

  writeText(directory + "/" + CredentialStore::kTemporaryName, "stale", 0600);
  check(store.erase(error) && store.load(loaded) == StoreStatus::Missing, "erase removes the record");
  check(access((directory + "/" + CredentialStore::kTemporaryName).c_str(), F_OK) != 0, "erase removes a temporary");
  check(store.erase(error), "erasing nothing succeeds");
  check(CredentialStore(root + "/never-created").erase(error), "erasing a missing store succeeds");
  check(store.save(profile, error), "store usable after erase");

  if (geteuid() == 0) {
    check(chown(file.c_str(), 1234, 1234) == 0 && store.load(loaded) == StoreStatus::Unsafe,
          "foreign-owned record refused");
    check(chown(file.c_str(), 0, 0) == 0 && chown(directory.c_str(), 1234, 1234) == 0 &&
              store.load(loaded) == StoreStatus::Unsafe, "foreign-owned directory refused");
    check(chown(directory.c_str(), 0, 0) == 0, "ownership restored");
  }
}

void testSupplicantConfig() {
  WifiProfile profile;
  makeProfile(std::string("x\"\n}\nnetwork={\0", 15), "password", profile);
  const std::string config = supplicantConfig(profile, "/tmp/awtrix-tc002d/wpa/ctrl", false);
  const std::string expected =
      "ctrl_interface=/tmp/awtrix-tc002d/wpa/ctrl\n"
      "update_config=0\n"
      "network={\n"
      "\tssid=78220a7d0a6e6574776f726b3d7b00\n"
      "\tkey_mgmt=WPA-PSK WPA-PSK-SHA256\n"
      "\tieee80211w=1\n"
      "\tpsk=" + hex(profile.psk) + "\n"
      "}\n";
  check(config == expected,
        "config is exactly one hex network block that also accepts WPA-PSK-SHA256 and optional PMF, "
        "not probed for by name");
  check(config.find("password") == std::string::npos && config.find('#') == std::string::npos,
        "no passphrase or comment in config");
  const std::string hidden = supplicantConfig(profile, "/tmp/awtrix-tc002d/wpa/ctrl", true);
  check(hidden == "ctrl_interface=/tmp/awtrix-tc002d/wpa/ctrl\n"
                  "update_config=0\n"
                  "network={\n"
                  "\tssid=78220a7d0a6e6574776f726b3d7b00\n"
                  "\tscan_ssid=1\n"
                  "\tkey_mgmt=WPA-PSK WPA-PSK-SHA256\n"
                  "\tieee80211w=1\n"
                  "\tpsk=" + hex(profile.psk) + "\n"
                  "}\n",
        "only a hidden network is probed for by name");
  WifiProfile open;
  makeProfile("Cafe", "", open);
  const std::string openConfig = supplicantConfig(open, "/run/x", false);
  check(openConfig.find("\tkey_mgmt=NONE\n") != std::string::npos && openConfig.find("psk=") == std::string::npos,
        "open network config");
  check(openConfig.find("ieee80211w") == std::string::npos, "an open network has no PMF");
  check(supplicantConfig(profile, "relative/ctrl", false).empty(), "relative control directory refused");
  check(supplicantConfig(profile, "/tmp/a b", false).empty(), "space in control directory refused");
  check(supplicantConfig(profile, "/tmp/a\nupdate_config=1", false).empty(), "newline in control directory refused");
  check(supplicantConfig(profile, "/tmp/../etc", false).empty(), "parent reference refused");
  WifiProfile empty;
  check(supplicantConfig(empty, "/tmp/x", true).empty(), "empty profile refused");
  check(scanOnlyConfig("/tmp/awtrix-tc002d/wpa/ctrl") == "ctrl_interface=/tmp/awtrix-tc002d/wpa/ctrl\nupdate_config=0\n",
        "scan-only config has no network");
  check(scanOnlyConfig("/tmp/a\nnetwork={").empty(), "scan-only config refuses unsafe paths");
  check(accessPointConfig("awtrix-test", "/run/x") ==
            "ctrl_interface=/run/x\nupdate_config=0\nnetwork={\n\tssid=6177747269782d74657374\n"
            "\tmode=2\n\tfrequency=2412\n\tkey_mgmt=NONE\n}\n", "open access point on channel one");
  check(accessPointConfig("", "/run/x").empty() && accessPointConfig(std::string(33, 'a'), "/run/x").empty() &&
            accessPointConfig("clock", "/run/../etc").empty(), "invalid access point arguments refused");
  WpaStatus status;
  check(parseStatus("mode=AP\nwpa_state=COMPLETED\n", status) && status.mode == "AP", "AP mode status");
  check(parseEvent("<3>AP-ENABLED").type == WpaEventType::AccessPointEnabled &&
            parseEvent("<3>AP-DISABLED").type == WpaEventType::AccessPointDisabled &&
            parseEvent("<3>AP-STA-CONNECTED 00:11:22:33:44:55").type == WpaEventType::StationConnected &&
            parseEvent("<3>AP-STA-DISCONNECTED 00:11:22:33:44:55").type == WpaEventType::StationDisconnected,
        "access point events");
}

void testWritePrivateFile(const std::string& root) {
  const std::string path = root + "/wpa.conf";
  int error = 0;
  check(writePrivateFile(path, "first\n", error) && readText(path) == "first\n", "private file written");
  struct stat info {};
  check(stat(path.c_str(), &info) == 0 && (info.st_mode & 07777) == 0600, "private file is 0600");
  check(writePrivateFile(path, "second\n", error) && readText(path) == "second\n", "private file replaced");
  check(access((path + ".tmp").c_str(), F_OK) != 0, "no temporary left");
}

void testControlParsing() {
  WpaStatus status;
  check(parseStatus("bssid=00:11:22:33:44:55\nfreq=2437\nssid=Caf\\xc3\\xa9 \\\"x\\\"\nid=0\nmode=station\n"
                    "pairwise_cipher=CCMP\nwpa_state=COMPLETED\naddress=02:00:00:00:00:07\n", status) &&
            status.wpaState == "COMPLETED" && status.ssid == "Caf\xc3\xa9 \"x\"" && status.address == "02:00:00:00:00:07",
        "STATUS parsed with printf-decoded ssid");
  check(!parseStatus("FAIL\n", status), "FAIL is not a status");
  check(parseStatus("wpa_state=SCANNING\n", status) && status.ssid.empty(), "scanning status");
  int rssi = 0;
  check(parseSignalPoll("RSSI=-52\nLINKSPEED=65\nNOISE=9999\nFREQUENCY=2437\n", rssi) && rssi == -52, "SIGNAL_POLL");
  check(!parseSignalPoll("FAIL\n", rssi) && !parseSignalPoll("RSSI=abc\n", rssi), "bad SIGNAL_POLL refused");

  check(parseEvent("<3>CTRL-EVENT-CONNECTED - Connection to 00:11:22:33:44:55 completed [id=0 id_str=]").type ==
            WpaEventType::Connected, "connected event");
  check(parseEvent("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=3").type == WpaEventType::Disconnected,
        "disconnected event");
  check(parseEvent("IFNAME=wlan0 <3>CTRL-EVENT-CONNECTED - Connection").type == WpaEventType::Connected,
        "global interface prefix");
  const WpaEvent wrong =
      parseEvent("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Home\" auth_failures=1 duration=10 reason=WRONG_KEY");
  check(wrong.type == WpaEventType::TemporarilyDisabled && wrong.wrongKey, "wrong key detected");
  const WpaEvent spoof = parseEvent(
      "<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"a reason=WRONG_KEY\" auth_failures=1 duration=10 reason=CONN_FAILED");
  check(spoof.type == WpaEventType::TemporarilyDisabled && !spoof.wrongKey, "ssid text cannot fake a wrong key");
  check(parseEvent("<3>CTRL-EVENT-TERMINATING").type == WpaEventType::Terminating, "terminating event");
  check(parseEvent("<2>CTRL-EVENT-BSS-ADDED 1 00:11:22:33:44:55").type == WpaEventType::Other, "other event");

  check(parseEvent("<2>CTRL-EVENT-SCAN-RESULTS ").type == WpaEventType::ScanResults, "scan results event");
  const std::string scan =
      "bssid / frequency / signal level / flags / ssid\n"
      "00:11:22:33:44:01\t2412\t-71\t[WPA2-PSK-CCMP][ESS]\tHome\n"
      "00:11:22:33:44:02\t2437\t-48\t[ESS]\tHome\n"
      "00:11:22:33:44:03\t2462\t-60\t[ESS]\tCaf\\xc3\\xa9 \\\"5G\\\"\n"
      "00:11:22:33:44:04\t2462\t-30\t[WPA2-PSK-CCMP][ESS]\t\n"
      "00:11:22:33:44:05\t2462\t-40\t[ESS]\tbad\\xff\n"
      "00:11:22:33:44:06\t2462\t-50\t[WEP][ESS]\tOld\n"
      "00:11:22:33:44:07\t5180\t-55\t[RSN-SAE-CCMP][ESS]\tNew\n"
      "00:11:22:33:44:08\t5180\t-65\t[WPA-PSK-TKIP][ESS]\tLegacy\n"
      "00:11:22:33:44:09\t5180\t-58\t[ESS]\tline\\nbreak\n"
      "garbage line\n"
      "00:11:22:33:44:0a\t5180\tloud\t[ESS]\tNoise\n"
      "00:11:22:33:44:0b\t5180\t12\t[ESS]\tQuality\n";
  const std::vector<awtrix::tc002::WifiNetwork> networks = parseScanResults(scan);
  check(networks.size() == 6, "hidden, binary, control-character and malformed rows dropped");
  if (networks.size() == 6) {
    check(networks[0].ssid == "Quality" && networks[0].rssi == 0, "positive level clamps to 0");
    check(networks[1].ssid == "Home" && networks[1].rssi == -48 && !networks[1].secure,
          "duplicate SSID keeps the strongest entry and its flags");
    check(networks[2].ssid == "Old" && networks[2].secure, "WEP is secure");
    check(networks[3].ssid == "New" && networks[3].secure, "SAE is secure");
    check(networks[4].ssid == "Caf\xc3\xa9 \"5G\"" && !networks[4].secure, "escaped UTF-8 SSID decoded");
    check(networks[5].ssid == "Legacy" && networks[5].secure, "WPA is secure");
  }
  std::string many = "bssid / frequency / signal level / flags / ssid\n";
  for (int i = 0; i < 40; ++i) {
    char row[96];
    std::snprintf(row, sizeof row, "00:11:22:33:44:%02x\t2412\t-%d\t[ESS]\tNet%02d\n", i, 30 + i, i);
    many += row;
  }
  const auto capped = parseScanResults(many);
  check(capped.size() == 32 && capped.front().ssid == "Net00" && capped.back().ssid == "Net31", "capped at 32 strongest");
  check(parseScanResults("FAIL\n").empty(), "failure reply gives no networks");

  check(decodePrintfEncoded("a\\\\b\\e\\n\\r\\t\\x41\\x4") == std::string("a\\b\033\n\r\tA\\x4"), "printf decode");
  check(displaySsid("Caf\xc3\xa9") == "Caf\xc3\xa9", "valid UTF-8 displayed as is");
  check(displaySsid(std::string("a\xff\x01\\", 4)) == "a\\xff\\x01\\\\", "binary ssid escaped");
  check(displaySsid("\xc0\xaf") == "\\xc0\\xaf", "overlong UTF-8 escaped");
}

void testSystemParsing() {
  const std::string header =
      "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";
  const std::string listen5555 =
      "   0: 00000000:15B3 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 4711 1 c0 100 0 0 10 0\n";
  const std::string established5555 =
      "   1: 6EB2A8C0:15B3 0AB2A8C0:D431 01 00000000:00000000 00:00000000 00000000     0        0 4712 1 c0 20 4 30 10 -1\n";
  const std::string listen80 =
      "   2: 00000000:0050 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 4713 1 c0 100 0 0 10 0\n";
  check(countTcpListeners(header, 5555) == 0, "empty table");
  check(countTcpListeners(header + listen5555 + established5555 + listen80, 5555) == 1, "one 5555 listener");
  check(countTcpListeners(header + established5555 + listen80, 5555) == 0, "established 5555 is not a listener");
  const std::string six =
      "   0: 00000000000000000000000000000000:15B3 00000000000000000000000000000000:0000 0A 00000000:00000000 "
      "00:00000000 00000000     0        0 99 1 c0 100 0 0 10 0\n";
  check(countTcpListeners(header + six, 5555) == 1, "tcp6 listener");
  check(countTcpListeners("garbage\n", 5555) < 0, "missing header refused");
  check(countTcpListeners(header + "   0: 0000000G:15B3 00000000:0000 0A\n", 5555) < 0, "bad hex refused");
  check(countTcpListeners(header + "   0: 00000000:15B3\n", 5555) < 0, "short row refused");

}

void testWriteAttribute(const std::string& root) {
  const std::string path = root + "/aicwf_dbg_level";
  writeText(path, "1\n", 0660);
  check(writeAttribute(path, "0") == 0 && readText(path) == "0", "attribute replaced, not appended to");
  check(writeAttribute(root + "/missing", "0") == ENOENT, "missing attribute reports ENOENT");
  const std::string link = root + "/level-link";
  check(symlink(path.c_str(), link.c_str()) == 0 && writeAttribute(link, "1") == ELOOP && readText(path) == "0",
        "a symlinked attribute is not followed");
}

}

int main() {
  char pattern[] = "/tmp/tc002-wifi-units-XXXXXX";
  const char* root = mkdtemp(pattern);
  if (!root) { std::perror("mkdtemp"); return 1; }
  testSha1AndHmac();
  testPbkdf2();
  testProfiles();
  testRecordCodec();
  testStore(root);
  testSupplicantConfig();
  testWritePrivateFile(root);
  testControlParsing();
  testSystemParsing();
  testWriteAttribute(root);
  const std::string cleanup = std::string("rm -rf '") + root + "'";
  if (std::system(cleanup.c_str()) != 0) std::fprintf(stderr, "cleanup failed\n");
  std::printf("tc002 wifi units: %u checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
