#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

typedef struct bvm bvm;

namespace awtrix::script {

class BerryVM {
 public:
  // Berry instructions one protected call may run before it is aborted. Enforced by the
  // VM heartbeat hook, so the granularity is a heartbeat period, not a single instruction.
  static constexpr long kInstructionLimit = 200000;

  BerryVM();
  ~BerryVM();
  BerryVM(const BerryVM&) = delete;
  BerryVM& operator=(const BerryVM&) = delete;

  bool load(const std::string& source);
  bool loadSolidifiedPrelude();

  // Source must end with `return YourApp()`. `implemented` comes back as a bitmask over
  // `methods`: bit i is set when methods[i] resolved on the returned instance.
  bool loadApp(const std::string& appKey, const std::string& source,
               const char* const* methods, int methodCount, uint32_t& implemented);

  bool loadModule(const std::string& importName, const std::string& source);
  void dropModule(const std::string& importName);

  bool method(const std::string& appKey, const char* name);
  bool method1(const std::string& appKey, const char* name, const std::string& a);
  bool method1Bool(const std::string& appKey, const char* name, const std::string& a, bool& out);
  bool methodString(const std::string& appKey, const char* name, std::string& out);
  bool methodBool(const std::string& appKey, const char* name, bool& out);
  bool methodInt(const std::string& appKey, const char* name, long& out);

  bool dropApp(const std::string& appKey);

  void gcCollect();

  bool hasFunction(const char* name) const;
  bool call(const char* name);
  bool call1(const char* name, const std::string& a);
  bool call2(const char* name, const std::string& a, const std::string& b);
  bool call3(const char* name, const std::string& a, const std::string& b,
             const std::string& c);
  bool callString(const char* name, std::string& out);
  const std::string& lastError() const { return err_; }
  bvm* raw() { return vm_; }

  std::size_t heapBytes() const;

 private:
  bool doCall(const char* name, int argc, const std::string* a, const std::string* b,
              const std::string* c, std::string* out = nullptr);
  bool doMethod(const std::string& appKey, const char* name, int argc,
                const std::string* a, std::string* out = nullptr,
                bool* boolOut = nullptr, long* intOut = nullptr);
  bool captureError(int rc);

  bvm* vm_ = nullptr;
  std::string err_;
  // Non-empty until the prelude has run. Loading an app or a module refuses while it is
  // set, because both rely on prelude globals such as _app_anchor.
  std::string bootstrapErr_;
};

}
