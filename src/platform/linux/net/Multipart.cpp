#include "platform/linux/net/Multipart.h"

#include <algorithm>
#include <random>
#include <utility>

namespace awtrix::net {
namespace {

constexpr char kBoundaryPrefix[] = "form-boundary-";
constexpr char kBoundaryAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr std::size_t kBoundaryRandomChars = 24;
constexpr char kLineEnd[] = "\r\n";

bool quotable(const std::string& text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
    return c >= 32 && c < 127 && c != '"' && c != '\\';
  });
}

bool holds(const std::vector<FormField>& fields, const FormFile& file, const std::string& boundary) {
  return file.bytes.find(boundary) != std::string_view::npos ||
         std::any_of(fields.begin(), fields.end(),
                     [&](const FormField& field) { return field.value.find(boundary) != std::string::npos; });
}

std::string fieldHead(const std::string& boundary, const FormField& field) {
  return "--" + boundary + kLineEnd + "Content-Disposition: form-data; name=\"" + field.name + "\"" + kLineEnd +
         kLineEnd;
}

std::string fileHead(const std::string& boundary, const FormFile& file) {
  return "--" + boundary + kLineEnd + "Content-Disposition: form-data; name=\"" + file.field + "\"; filename=\"" +
         file.fileName + "\"" + kLineEnd + "Content-Type: " + file.contentType + kLineEnd + kLineEnd;
}

std::string closing(const std::string& boundary) { return std::string(kLineEnd) + "--" + boundary + "--" + kLineEnd; }

}

std::string randomBoundary() {
  std::random_device random;
  std::uniform_int_distribution<std::size_t> pick(0, sizeof(kBoundaryAlphabet) - 2);
  std::string boundary = kBoundaryPrefix;
  for (std::size_t i = 0; i < kBoundaryRandomChars; ++i) boundary += kBoundaryAlphabet[pick(random)];
  return boundary;
}

std::optional<Form> buildMultipart(const std::vector<FormField>& fields, const FormFile& file,
                                   const std::function<std::string()>& boundaries) {
  if (!quotable(file.field) || !quotable(file.fileName) || !quotable(file.contentType) ||
      !std::all_of(fields.begin(), fields.end(), [](const FormField& field) { return quotable(field.name); }))
    return std::nullopt;
  std::string boundary = boundaries();
  while (holds(fields, file, boundary)) boundary = boundaries();
  Form form;
  for (const FormField& field : fields) {
    form.head += fieldHead(boundary, field);
    form.head += field.value;
    form.head += kLineEnd;
  }
  form.head += fileHead(boundary, file);
  form.tail = closing(boundary);
  form.contentType = "multipart/form-data; boundary=" + std::move(boundary);
  return form;
}

}
