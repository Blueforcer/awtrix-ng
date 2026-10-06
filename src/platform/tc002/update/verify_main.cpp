#include "platform/tc002/update/PackageVerifier.h"

#include "core/api/JsonText.h"

#include <charconv>
#include <iostream>
#include <map>
#include <string>

namespace {
bool number(const std::string& value, std::uint64_t& result) {
  if (value.empty()) return false;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size();
}
std::string quote(const std::string& value) {
  std::string out;
  awtrix::api::appendJsonString(out, value);
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string usage =
      "awtrix-update-verify --package FILE --target TARGET "
      "--current-counter N [--max-payload-bytes N] [--stage-dir PRIVATE_DIR]\n"
      "Verifies/stages an unsigned AWUPD003 container only. No production installation or flash writes.\n";
  if (argc == 2 && std::string(argv[1]) == "--help") { std::cout << usage; return 0; }
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; i += 2) {
    const std::string option = argv[i];
    if (i + 1 == argc || (option != "--package" && option != "--target" &&
        option != "--current-counter" && option != "--max-payload-bytes" && option != "--stage-dir") ||
        !args.emplace(option, argv[i + 1]).second) {
      std::cerr << usage; return 2;
    }
  }
  awtrix::tc002::update::Policy policy;
  if (!args.count("--package") || !args.count("--target") ||
      !args.count("--current-counter") || !number(args["--current-counter"], policy.currentCounter) ||
      (args.count("--max-payload-bytes") && !number(args["--max-payload-bytes"], policy.maxPayloadBytes))) {
    std::cerr << usage; return 2;
  }
  policy.expectedTarget = args["--target"];
  const auto result = awtrix::tc002::update::verify(args["--package"], policy, args["--stage-dir"]);
  if (!result.ok) {
    std::cout << "{\"ok\":false,\"code\":" << quote(result.code) << ",\"error\":" << quote(result.error) << "}\n";
    return 1;
  }
  std::cout << "{\"ok\":true,\"target\":" << quote(result.target)
            << ",\"release\":" << quote(result.release) << ",\"counter\":" << result.counter
            << ",\"payload_bytes\":" << result.payloadBytes
            << ",\"payload_sha256\":" << quote(result.payloadSha256)
            << ",\"staged_file\":" << quote(result.stagedFile) << "}\n";
  return 0;
}
