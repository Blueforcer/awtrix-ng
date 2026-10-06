#include "platform/linux/host/HostHttpBody.h"

#include <algorithm>

#include "core/api/ApiRouter.h"

namespace awtrix::host_http {

void sendJson(httplib::Response& res, int status, const std::string& body) {
  res.status = status;
  res.set_content(body, "application/json");
}

void sendError(httplib::Response& res, int status, const char* code, const char* message) {
  sendJson(res, status, api::errorJson(code, message));
}

// Bodies above this size are read one at a time.
constexpr uint64_t kConcurrentBodyBytes = 64 * 1024;

bool readsAlone(const httplib::Request& req) {
  if (req.has_header("Transfer-Encoding")) return true;
  const uint64_t length = req.get_header_value_u64("Content-Length");
  return length > kConcurrentBodyBytes;
}

// Reads at most maxBodyBytes, including chunked bodies; declared sizes use one buffer.
// Form-encoded bodies stay raw.
bool readBody(const httplib::ContentReader& content, httplib::Request& req, httplib::Response& res,
              uint64_t maxBodyBytes) {
  const uint64_t declared = req.has_header("Transfer-Encoding")
                                ? 0 : req.get_header_value_u64("Content-Length");
  const std::size_t presize = declared <= maxBodyBytes ? static_cast<std::size_t>(declared) : 0;
  std::size_t received = 0;
  bool tooLarge = false;
  const auto keep = [&](std::string& into, const char* data, std::size_t size) {
    if (size > maxBodyBytes - received) {
      tooLarge = true;
      return false;
    }
    received += size;
    into.append(data, size);
    return true;
  };
  bool ok;
  if (req.is_multipart_form_data()) {
    auto part = req.files.end();
    bool presized = false;
    ok = content(
        [&](const httplib::MultipartFormData& header) {
          if (req.files.size() == CPPHTTPLIB_MULTIPART_FORM_DATA_FILE_MAX_COUNT) return false;
          part = req.files.emplace(header.name, header);
          if (!presized && !header.filename.empty()) {
            part->second.content.reserve(presize - std::min(presize, received));
            presized = true;
          }
          return true;
        },
        [&](const char* data, std::size_t size) { return keep(part->second.content, data, size); });
  } else {
    req.body.reserve(presize);
    ok = content([&](const char* data, std::size_t size) { return keep(req.body, data, size); });
  }
  if (tooLarge) res.status = 413;
  return ok;
}

void refuseBody(httplib::Response& res, uint64_t maxBodyBytes) {
  res.set_header("Connection", "close");
  sendError(res, 413, "payloadTooLarge",
            ("body exceeds " + std::to_string(maxBodyBytes) + " bytes").c_str());
}

// As httplib past its own limit: the refusal reaches a client that is still sending.
void discardBody(const httplib::Request& req, const httplib::ContentReader& content) {
  if (req.is_multipart_form_data())
    content([](const httplib::MultipartFormData&) { return true; }, [](const char*, std::size_t) { return true; });
  else
    content([](const char*, std::size_t) { return true; });
}

}
