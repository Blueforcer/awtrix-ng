#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix::net {

struct FormField {
  std::string name, value;
};
struct FormFile {
  std::string field, fileName, contentType;
  std::string_view bytes;
};
struct Form {
  std::string contentType, head, tail;
};

std::string randomBoundary();

std::optional<Form> buildMultipart(const std::vector<FormField>& fields, const FormFile& file,
                                   const std::function<std::string()>& boundaries = randomBoundary);

}
