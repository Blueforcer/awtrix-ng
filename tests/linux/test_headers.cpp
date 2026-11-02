#include "../support.h"
#include "ThreadStacks.h"
// Parser contracts for the AWTRIX cpp-httplib 0.20.0 hardening patch.
#include "platform/linux/host/vendor/httplib.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr auto check = awtrix::test::require;

void parserChecks() {
  const std::string header = "X-Fixture: report%20name%0D%0Avalue";
  std::string value;
  check(httplib::detail::parse_header(header.data(), header.data() + header.size(),
      [&](const std::string&, const std::string& parsed) { value = parsed; }) &&
      value == "report%20name%0D%0Avalue", "header values retain literal percent escapes");

  httplib::detail::BufferStream stream;
  const std::string chunks = "3\r\nabc\r\n0\r\nX-Fixture: trailer\r\n\r\n";
  stream.write(chunks.data(), chunks.size());
  httplib::Request request;
  request.set_header("X-Fixture", "original");
  std::string content;
  check(httplib::detail::read_content_chunked(stream, request,
      [&](const char* data, size_t size, uint64_t, uint64_t) {
        content.append(data, size); return true;
      }) && content == "abc" && request.get_header_value_count("X-Fixture") == 1 &&
      request.get_header_value("X-Fixture") == "original", "trailers do not modify initial headers");

  for (const std::string& filename : {std::string("UTF-8''report%20name.bin"), "utf-8''" + std::string(7000, 'a')}) {
    httplib::detail::MultipartFormDataParser parser;
    parser.set_boundary("fixture");
    std::string parsed_name, parsed_filename, parsed_body;
    const std::string body = "--fixture\r\ncOnTeNt-dIsPoSiTiOn:\tFoRm-DaTa; name=\"file\"; filename*=\"" +
        filename + "\"\r\n\r\nabc\r\n--fixture--\r\n";
    const bool okay = parser.parse(body.data(), body.size(), [&](const char* data, size_t size) {
      parsed_body.append(data, size); return true;
    }, [&](const httplib::MultipartFormData& part) {
      parsed_name = part.name; parsed_filename = part.filename; return true;
    });
    check(okay && parser.is_valid() && parsed_name == "file" && parsed_body == "abc" &&
        parsed_filename == httplib::detail::decode_url(filename.substr(7), false),
        "multipart filenames parse without recursive regular expressions");
  }
}

std::string line(size_t bytes) {
  return "X: " + std::string(bytes - 5, 'a') + "\r\n";
}

bool headers(const std::string& text, httplib::Headers& output) {
  httplib::detail::BufferStream input;
  input.write(text.data(), text.size());
  return httplib::detail::read_headers(input, output);
}

void statusLines() {
  struct Line {
    std::string text;
    bool valid;
    const char* version;
    int status;
    const char* reason;
  };
  const std::string longReason(16000, 'r');
  const std::vector<Line> lines = {
      {"HTTP/1.1 200 OK\r\n", true, "HTTP/1.1", 200, "OK"},
      {"HTTP/1.0 404 Not Found\n", true, "HTTP/1.0", 404, "Not Found"},
      {"ICY 200 OK\r\n", true, "HTTP/1.0", 200, "OK"},
      {"HTTP/1.1 204\r\n", true, "HTTP/1.1", 204, ""},
      {"HTTP/1.1 200 \r\n", true, "HTTP/1.1", 200, ""},
      {"HTTP/1.1 200 " + longReason + "\r\n", true, "HTTP/1.1", 200, longReason.c_str()},
      {"", false, "", 0, ""},
      {"HTTP/1.1 200 OK", false, "", 0, ""},
      {"HTTP/1.1 200 OK\r\r\n", false, "", 0, ""},
      {"HTTP/1.1 20 OK\r\n", false, "", 0, ""},
      {"HTTP/1.1 2000 OK\r\n", false, "", 0, ""},
      {"HTTP/1.1 20x OK\r\n", false, "", 0, ""},
      {"HTTP/1.1  200 OK\r\n", false, "", 0, ""},
      {"HTTP/1.1 200\tOK\r\n", false, "", 0, ""},
      {"HTTP/2 200 OK\r\n", false, "", 0, ""},
      {"http/1.1 200 OK\r\n", false, "", 0, ""},
      {"ICY200 OK\r\n", false, "", 0, ""},
  };
  for (const Line& line : lines) {
    httplib::Response response;
    const bool valid = httplib::detail::parse_status_line(line.text.c_str(), response);
    check(valid == line.valid && (!valid || (response.version == line.version && response.status == line.status &&
                                             response.reason == line.reason)),
          "status line: " + line.text.substr(0, 40));
  }
}

void locations() {
  struct Case {
    std::string text;
    bool valid;
    const char* scheme;
    const char* host;
    int port;
    const char* path;
    const char* query;
  };
  const std::string longPath = "/" + std::string(16000, 'a');
  const std::vector<Case> cases = {
      {"/x", true, "", "", -1, "/x", ""},
      {"x/y?q=1#f", true, "", "", -1, "x/y", "?q=1"},
      {"http://a.b/c", true, "http", "a.b", -1, "/c", ""},
      {"https://a.b:8443/c?d#e", true, "https", "a.b", 8443, "/c", "?d"},
      {"//a.b/c", true, "", "a.b", -1, "/c", ""},
      {"http://[::1]:8080/c", true, "http", "::1", 8080, "/c", ""},
      {"http://[zz]/c", true, "http", "[zz]", -1, "/c", ""},
      {"http://[::1/c", true, "http", "[", -1, "::1/c", ""},
      {"http://a.b:x/c", true, "http", "a.b", -1, ":x/c", ""},
      {"http://a.b:80x", true, "http", "a.b", 80, "x", ""},
      {"http://a.b:65535/", true, "http", "a.b", 65535, "/", ""},
      {"http:x", true, "http", "", -1, "x", ""},
      {"httpx://a/b", true, "", "", -1, "httpx://a/b", ""},
      {"///x", true, "", "", -1, "///x", ""},
      {"?q", true, "", "", -1, "", "?q"},
      {"#f", true, "", "", -1, "", ""},
      {"", true, "", "", -1, "", ""},
      {longPath, true, "", "", -1, longPath.c_str(), ""},
      {"http://a.b:65536/", false, "", "", 0, "", ""},
      {"http://a.b:99999999999/x", false, "", "", 0, "", ""},
      {"/x#a\rb", false, "", "", 0, "", ""},
  };
  for (const Case& one : cases) {
    httplib::detail::LocationParts parts;
    const bool valid = httplib::detail::parse_location(one.text, parts);
    check(valid == one.valid && (!valid || (parts.scheme == one.scheme && parts.host == one.host &&
                                            parts.port == one.port && parts.path == one.path &&
                                            parts.query == one.query)),
          "location: " + one.text.substr(0, 40));
  }
}

void matchAllRoutes() {
  const awtrix::test::ThreadStacks small(awtrix::test::kSmallStackBytes);
  httplib::Server server;
  const auto answer = [](const httplib::Request&, httplib::Response& response) {
    response.set_content("ok", "text/plain");
  };
  server.Get(".*", answer);
  server.Post(".*", answer);
  server.Put(".*", answer);
  server.Patch(".*", answer);
  server.Delete(".*", answer);
  const int port = server.bind_to_any_port("127.0.0.1");
  std::thread listener([&server] { server.listen_after_bind(); });
  server.wait_until_ready();

  httplib::Client client("127.0.0.1", port);
  const std::string longPath = "/" + std::string(CPPHTTPLIB_REQUEST_URI_MAX_LENGTH - 1, 'a');
  const auto status = [](const httplib::Result& result) { return result ? result->status : -1; };
  for (const std::string& path : {std::string("/"), std::string("/a/b?c=d"), std::string("/%00"), longPath}) {
    const std::string name = "match-all route answers " + path.substr(0, 40);
    check(status(client.Get(path)) == 200, "GET " + name);
    check(status(client.Post(path, "x", "text/plain")) == 200, "POST " + name);
    check(status(client.Put(path, "x", "text/plain")) == 200, "PUT " + name);
    check(status(client.Patch(path, "x", "text/plain")) == 200, "PATCH " + name);
    check(status(client.Delete(path)) == 200, "DELETE " + name);
  }
  for (const char* path : {"/a%0Ab", "/a%0Db", "/%0A", "/%0D"}) {
    check(status(client.Get(path)) == 404, std::string("match-all route refuses ") + path);
    check(status(client.Post(path, "x", "text/plain")) == 404, std::string("POST match-all route refuses ") + path);
  }
  check(status(client.Get(longPath + "a")) == 414, "a path above the limit stays refused");
  server.stop();
  listener.join();
}

bool chunks(const std::string& text, httplib::Response& response, std::string& body) {
  httplib::detail::BufferStream input;
  input.write(text.data(), text.size());
  return httplib::detail::read_content_chunked(input, response,
      [&](const char* data, size_t size, uint64_t, uint64_t) { body.append(data, size); return true; });
}
}

int main() {
  parserChecks();
  statusLines();
  locations();
  matchAllRoutes();
  constexpr size_t maxLine = CPPHTTPLIB_HEADER_MAX_LENGTH;
  constexpr size_t maxBytes = CPPHTTPLIB_HEADER_MAX_TOTAL_LENGTH;
  constexpr size_t maxCount = CPPHTTPLIB_HEADER_MAX_COUNT;
  {
    httplib::detail::BufferStream input;
    const std::string attack(1024 * 1024, 'a');
    input.write(attack.data(), attack.size());
    char fixed[16] {};
    httplib::detail::stream_line_reader reader(input, fixed, sizeof(fixed));
    check(!reader.getline(), "unterminated line accepted");
    check(reader.size() == maxLine, "reader grew beyond its limit");
    check(!reader.getline(0) && reader.size() == 0, "zero-budget reader consumed input");
  }
  {
    httplib::Headers output;
    check(headers(line(maxLine) + "\r\n", output), "exact line limit rejected");
    output.clear();
    check(!headers(line(maxLine + 1) + "\r\n", output) && output.empty(), "long line allocated a header");
  }
  {
    std::string text;
    for (size_t i = 0; i < maxCount; ++i) text += "X: a\r\n";
    httplib::Headers output;
    check(headers(text + "\r\n", output) && output.size() == maxCount, "exact count limit rejected");
    output.clear();
    check(!headers(text + "X: a\r\n", output), "incomplete many-header stream accepted");
    check(output.size() == maxCount, "headers grew beyond count limit");
  }
  {
    std::string text;
    while (text.size() + maxLine < maxBytes - 2) text += line(maxLine);
    text += line(maxBytes - 2 - text.size());
    httplib::Headers output;
    check(headers(text + "\r\n", output), "exact total-byte limit rejected");
    output.clear();
    check(!headers(text + "X: a\r\n", output), "total-byte overflow accepted");
    size_t stored = 0;
    for (const auto& header : output) stored += header.first.size() + header.second.size();
    check(stored <= maxBytes, "parsed storage exceeds bounded wire input");
  }
  {
    httplib::Response response;
    std::string body;
    check(chunks("3\r\nabc\r\n0\r\nX: valid\r\n\r\n", response, body) && body == "abc" &&
          response.headers.empty(), "valid chunked trailers altered response headers");
    response.headers.clear();
    check(!chunks("0\r\nX: " + std::string(maxLine, 'x'), response, body), "long trailer accepted");
    check(response.headers.empty(), "long trailer allocated a header");
    std::string many = "0\r\n";
    for (size_t i = 0; i <= maxCount; ++i) many += "X: a\r\n";
    check(!chunks(many, response, body) && response.headers.empty(),
          "trailer count not bounded");
    response.headers.clear();
    check(!chunks("0\r\n", response, body), "missing trailer terminator accepted");
    check(!chunks("0;" + std::string(maxLine, 'a'), response, body), "long chunk metadata accepted");
    for (const auto* size : {"-2", "+3", " 3"}) {
      body.clear();
      check(!chunks(std::string(size) + "\r\nabc\r\n0\r\n\r\n", response, body) && body.empty(),
            "invalid chunk size reached the body callback");
    }
  }
  std::cout << "bounded line, header count/bytes, response parser and chunked trailer contracts passed\n";
}
