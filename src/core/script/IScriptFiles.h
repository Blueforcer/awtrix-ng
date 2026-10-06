#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/script/ScriptServices.h"

namespace awtrix::script {

class IScriptFiles : public IScriptStoreSink {
 public:
  using LoadFn = std::function<void(const std::string& name, const std::string& source,
                                   const std::string& storeJson)>;
  virtual void save(const std::string& name, const std::string& source) = 0;
  virtual void remove(const std::string& name) = 0;
  virtual void loadAll(const LoadFn& cb) = 0;
  virtual std::vector<std::string> names() const = 0;
  virtual bool readSource(const std::string& name, std::string& out) const = 0;
  virtual bool readStore(const std::string& name, std::string& out) const = 0;
};

}
