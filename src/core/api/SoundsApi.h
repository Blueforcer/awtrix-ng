#pragma once

#include <cstdint>
#include <string>


namespace awtrix {
namespace api {
namespace sounds {

std::string nameFromFile(const std::string& fileName);

std::string pathFor(const std::string& name);

std::string entryJson(const std::string& name, const std::string& content, uint32_t bytes);

struct PutResult {
  bool ok = false;
  int status = 422;
  std::string code;
  std::string message;
  std::string field;
  std::string content;
};

PutResult prepareWrite(const std::string& name, const std::string& body);

}
}
}
