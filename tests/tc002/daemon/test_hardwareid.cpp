// The activation id: the recipe Ulanzi recomputes from its own records, the blank ids no clock
// reports, and the reasons readUniqueId gives where the flash driver is missing or not a driver.
#include <string>

#include "platform/tc002/daemon/HardwareId.h"
#include "support.h"

using namespace awtrix::tc002d;
using tc002d_test::check;
using tc002d_test::TempDir;
using tc002d_test::writeFile;

namespace {

UniqueId fromHex(const std::string& hex) {
  UniqueId id{};
  for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<std::uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
  return id;
}

void knownClocks() {
  check(activationId(fromHex("525a5b4c7e36115a3c705e7f4cb63878")) ==
            "5a0604d88a74cfbff3dea6294267c6f03cc668b4664f120e0eb461c00c2075e7",
        "first test clock");
  check(activationId(fromHex("8fa2a4cb5ebe5ceccb0eddf436c4f4a2")) ==
            "f846b5c29dfe163307ab3fb5b1805d1142318c88eebcdfe371c6cc3b1a03fb17",
        "second test clock");
}

void blankIds() {
  UniqueId zeros{};
  UniqueId ones{};
  ones.fill(0xFF);
  check(activationId(zeros).empty(), "all zero bytes give no id");
  check(activationId(ones).empty(), "all 0xFF bytes give no id");
  UniqueId almost{};
  almost[15] = 1;
  check(activationId(almost).size() == 64, "one set bit is an id");
}

void unreadable() {
  TempDir dir;
  UniqueId id{};
  std::string error;
  check(!readUniqueId(dir / "missing", id, error), "a missing driver is refused");
  check(error.find("missing") != std::string::npos, "the error names the device");
  const std::string plain = dir / "plain";
  writeFile(plain, "not a driver");
  error.clear();
  check(!readUniqueId(plain, id, error), "a plain file is refused");
  check(error.rfind("unlock: ", 0) == 0, "a plain file fails at the unlock request");
}

}

int main() {
  tc002d_test::quietLogs();
  knownClocks();
  blankIds();
  unreadable();
  return tc002d_test::finish("tc002d-hardwareid");
}
