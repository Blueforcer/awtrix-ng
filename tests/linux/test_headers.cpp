#include "../support.h"
// Parser contracts for the AWTRIX cpp-httplib 0.20.0 hardening patch.
#include "platform/linux/host/vendor/httplib.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

bool chunks(const std::string& text, httplib::Response& response, std::string& body) {
  httplib::detail::BufferStream input;
  input.write(text.data(), text.size());
  return httplib::detail::read_content_chunked(input, response,
      [&](const char* data, size_t size, uint64_t, uint64_t) { body.append(data, size); return true; });
}
}

int main() {
  parserChecks();
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
