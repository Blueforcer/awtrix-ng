#include "../support.h"

#include <functional>
#include <string>
#include <vector>

#include "platform/linux/net/Multipart.h"

using awtrix::net::buildMultipart;
using awtrix::net::FormFile;
using awtrix::test::check;
using awtrix::test::require;

namespace {

std::function<std::string()> sequence(std::vector<std::string> boundaries, int& asked) {
  return [boundaries = std::move(boundaries), &asked] { return boundaries.at(static_cast<std::size_t>(asked++)); };
}

}

int main() {
  int asked = 0;
  auto form = buildMultipart({{"model", "whisper-1"}, {"language", "de"}},
                             {"file", "speech.wav", "audio/wav", "RIFF..."}, sequence({"B"}, asked));
  require(form.has_value(), "a form with two fields and a file");
  check(form->contentType == "multipart/form-data; boundary=B", "the content type names the boundary");
  check(form->head ==
            "--B\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
            "--B\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nde\r\n"
            "--B\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n",
        "the fields come first, then the head of the file part");
  check(form->tail == "\r\n--B--\r\n", "the file's bytes go between head and tail, which closes the form");

  asked = 0;
  form = buildMultipart({}, {"file", "a.bin", "application/octet-stream", "head B1 tail"},
                        sequence({"B1", "B2"}, asked));
  check(form && form->contentType == "multipart/form-data; boundary=B2" && asked == 2,
        "a boundary the file holds is replaced by the next candidate");
  asked = 0;
  form = buildMultipart({{"prompt", "say B1"}}, {"file", "a.bin", "application/octet-stream", "x"},
                        sequence({"B1", "B2"}, asked));
  check(form && form->contentType == "multipart/form-data; boundary=B2" && asked == 2,
        "and so is one a field value holds");

  form = buildMultipart({}, {"file", "a.bin", "application/octet-stream", std::string(4096, 'x')});
  const std::string prefix = "multipart/form-data; boundary=form-boundary-";
  require(form && form->contentType.rfind(prefix, 0) == 0, "a random boundary by default");
  const std::string boundary = form->contentType.substr(prefix.size());
  const std::string alphanumeric = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  check(boundary.size() == 24 && boundary.find_first_not_of(alphanumeric) == std::string::npos,
        "24 random letters and digits");
  const auto again = buildMultipart({}, {"file", "a.bin", "application/octet-stream", "x"});
  check(again && again->contentType != form->contentType, "each form draws its own boundary");

  const FormFile wav{"file", "speech.wav", "audio/wav", ""};
  check(!buildMultipart({{"a\"b", "x"}}, wav), "a name with a quote is refused");
  check(!buildMultipart({{"a\\b", "x"}}, wav), "a name with a backslash is refused");
  check(!buildMultipart({{"", "x"}}, wav), "an empty name is refused");
  check(!buildMultipart({}, {"file", "a\"b.wav", "audio/wav", ""}), "a file name with a quote is refused");
  check(!buildMultipart({}, {"file", "a\\b.wav", "audio/wav", ""}), "a file name with a backslash is refused");
  check(!buildMultipart({}, {"file", "", "audio/wav", ""}), "an empty file name is refused");
  check(!buildMultipart({}, {"f\r\n", "speech.wav", "audio/wav", ""}), "a file field with a line break is refused");
  check(!buildMultipart({}, {"file", "speech.wav", "audio/wav\r\nX-Evil: 1", ""}),
        "a content type with a line break is refused");
  return awtrix::test::finish("linux-multipart");
}
