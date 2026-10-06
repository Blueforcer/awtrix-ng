#include "../support.h"
// The crypto script module against a real script host.
#include <cstdio>
#include <memory>
#include <string>

#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/script/CryptoScripting.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

struct Rig {
  linux_script::CryptoScripting crypto{[] { return int64_t{0}; }};
  script::ScriptServices services;
  AppRegistry registry;
  std::unique_ptr<script::ScriptHost> host;
  std::unique_ptr<script::ExtensionHost> extensions;
  Rig() {
    services.monotonicMs = [] { return 0L; };
    host = std::make_unique<script::ScriptHost>(registry, services, nullptr, nullptr);
    extensions = std::make_unique<script::ExtensionHost>(*host,
        std::vector<script::ScriptExtension*>{&crypto});
  }
  std::string eval(const std::string& expr) {
    const std::string src = "import crypto\nclass App\ndef draw() end\ndef check() return " + expr +
                            " end\nend\nreturn App()";
    if (!host->set("C", src)) return "<no install>";
    auto* app = static_cast<script::ScriptApp*>(registry.find("C"));
    std::string out;
    if (!app || !app->callCheckForTest(out)) return "<no check>";
    return out;
  }
};

}

int main() {
  Rig r;
  check(r.eval("crypto.sha256('abc').tohex()") ==
            "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
        "sha256 of a string matches FIPS 180-2");
  check(r.eval("crypto.sha256(bytes('616263')).tohex()") ==
            "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
        "sha256 of bytes equals sha256 of the same string");
  check(r.eval("crypto.sha256('').tohex()") ==
            "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
        "sha256 of nothing");
  check(r.eval("crypto.sha256d('abc').tohex()") ==
            "4F8B42C22DD3729B519BA6F68D2DA7CC5B2D606D05DAED5AD5128CC03E6C6358",
        "sha256d is sha256 twice");
  check(r.eval("crypto.hmac_sha256('key', 'The quick brown fox jumps over the lazy dog').tohex()") ==
            "F7BC83F430538424B13298E6AA6FB143EF4D59A14946175997479DBC2D1A3CD8",
        "hmac_sha256 matches the RFC 4231 style reference");
  check(r.eval("crypto.hmac_sha256(bytes('00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'), 'x').size()") == "32",
        "a key longer than one block is hashed first");
  check(r.eval("(def () try crypto.sha256(5) return 'no' except 'value_error' return 'raised' end end)()") ==
            "raised",
        "a number is rejected");
  check(r.eval("crypto.sha256(bytes('00616263ff')).tohex()") == "3E3C7B3EE11557139E4AE2E4A7543A197C20FCF84FB7D67C1C3FEF8EAFD91244", "sha256 of binary bytes");
  check(r.eval("crypto.hmac_sha256(bytes('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'), 'Test Using Larger Than Block-Size Key - Hash Key First').tohex()") == "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54", "binary and RFC 4231 long-key vector");
  check(r.eval("crypto.md5('abc').tohex()") == "900150983CD24FB0D6963F7D28E17F72", "md5 matches RFC 1321");
  check(r.eval("crypto.md5(bytes()).tohex()") == "D41D8CD98F00B204E9800998ECF8427E", "md5 of empty bytes");
  check(r.eval("crypto.sha1('abc').tohex()") == "A9993E364706816ABA3E25717850C26C9CD0D89D", "sha1 matches FIPS 180");
  check(r.eval("crypto.hmac_sha1('Jefe', 'what do ya want for nothing?').tohex()") ==
            "EFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79",
        "hmac_sha1 matches RFC 2202");
  check(r.eval("crypto.hmac_sha256(bytes(), '').tohex()") ==
            "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD",
        "hmac_sha256 takes an empty key and message");
  check(r.eval("(def () var c = bytes() c.add(0, -4) c.add(59 / 30, -4) "
               "var h = crypto.hmac_sha1('12345678901234567890', c) "
               "return (h.get(h[19] & 0x0f, -4) & 0x7fffffff) % 100000000 end)()") == "94287082",
        "the building blocks give the RFC 6238 TOTP");
  check(r.eval("(def (secret, ms) import string var key = bytes(), bits = 0, n = 0 "
               "for i : 0 .. size(secret) - 1 "
               "var v = string.find('ABCDEFGHIJKLMNOPQRSTUVWXYZ234567', string.toupper(secret[i])) "
               "if v < 0 continue end bits = ((bits << 5) | v) & 0xfff n += 5 "
               "if n >= 8 n -= 8 key.add((bits >> n) & 0xff) end end "
               "var step = bytes() step.add(0, -4) step.add(ms / 30000, -4) "
               "var h = crypto.hmac_sha1(key, step) "
               "return string.format('%06d', (h.get(h[19] & 0x0f, -4) & 0x7fffffff) % 1000000) end)"
               "('gezdgnbvgy3tqojq GEZDGNBVGY3TQOJQ', 1111111109000)") == "081804",
        "the documented TOTP example gives RFC 6238's code");
  check(r.eval("bytes('48656c6c6f').tob64()") == "SGVsbG8=", "bytes encode Base64");
  check(r.eval("bytes().fromb64('SGVsbG8=').asstring()") == "Hello", "bytes decode Base64");
  check(r.eval("crypto.random(16).size()") == "16", "random returns the length asked for");
  check(r.eval("crypto.random(32) != crypto.random(32)") == "true", "random differs between calls");
  check(r.eval("crypto.pbkdf2_hmac_sha256('passwd', 'salt', 1, 64).tohex()") ==
            "55AC046E56E3089FEC1691C22544B605F94185216DDE0465E68B9D57C20DACBC"
            "49CA9CCCF179B645991664B39D77EF317C71B845B1E30BD509112041D3A19783",
        "pbkdf2 matches RFC 7914 with one iteration");
  check(r.eval("crypto.pbkdf2_hmac_sha256('Password', 'NaCl', 80000, 64).tohex()") ==
            "4DDCD8F60B98BE21830CEE5EF22701F9641A4418D04C0414AEFF08876B34AB56"
            "A1D425A1225833549ADB841B51C9B3176A272BDEBBA1D078478F62B397F33C8D",
        "pbkdf2 matches RFC 7914 with 80000 iterations");
  check(r.eval("(def () import string var p = string.split('2$10000$5A1711$2000$5A1722', '$') "
               "var h = crypto.pbkdf2_hmac_sha256('1example!', bytes(p[2]), int(p[1])) "
               "h = crypto.pbkdf2_hmac_sha256(h, bytes(p[4]), int(p[3])) "
               "return p[4] + '$' + string.tolower(h.tohex()) end)()") ==
            "5A1722$1798a1672bca7c6463d6b245f82b53703b0f50813401b03e4045a5861e689adb",
        "pbkdf2 answers the FRITZ!Box challenge of AVM's example");
  check(r.eval("crypto.pbkdf2_hmac_sha256('pw', '', 2, 40).tohex()") ==
            "75172DF23A569A0EC2563C23CC75D05AC5859EB93D81EBE4A26AF09F25C0E96E4F9578904EB95089",
        "pbkdf2 cuts the last block to the length and takes an empty salt");
  check(r.eval("crypto.pbkdf2_hmac_sha256('a', 'b', 3, nil) == crypto.pbkdf2_hmac_sha256('a', 'b', 3)") == "true",
        "pbkdf2 length nil means 32");
  for (const char* bad : {"crypto.pbkdf2_hmac_sha256('a', 'b', 0)", "crypto.pbkdf2_hmac_sha256('a', 'b')",
                          "crypto.pbkdf2_hmac_sha256('a', 'b', 1.5)", "crypto.pbkdf2_hmac_sha256('a', 'b', 1, 0)",
                          "crypto.pbkdf2_hmac_sha256('a', 'b', 1, 1025)",
                          "crypto.pbkdf2_hmac_sha256('a', 'b', 1000001)",
                          "crypto.pbkdf2_hmac_sha256('a', 'b', 500001, 33)", "crypto.random()",
                          "crypto.random(0)", "crypto.random(1025)", "crypto.hmac_sha1('k', nil)"}) {
    check(r.eval(std::string("(def () try ") + bad + " return 'no' except 'value_error' return 'raised' end end)()") ==
              "raised",
          bad);
  }
  return failures == 0 ? 0 : 1;
}
