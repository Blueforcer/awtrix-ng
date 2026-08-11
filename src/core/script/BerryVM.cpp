#include "core/script/BerryVM.h"

#include <string.h>

#include "berry.h"
#include "berry_conf.h"

extern "C" void awtrix_push_solidified_prelude(bvm* vm);
extern "C" void awtrix_module_cache_set(bvm* vm, const char* name);
extern "C" void awtrix_module_cache_drop(bvm* vm, const char* name);
extern "C" void be_throw(bvm* vm, int errorcode);
extern "C" size_t be_gc_memcount(bvm* vm);
extern "C" void be_gc_collect(bvm* vm);
extern "C" void be_gc_setsteprate(bvm* vm, int rate);
struct blexer;
extern "C" int be_protectedparser(bvm* vm, const char* fname,
                                  const char* (*reader)(struct blexer*, void*, size_t*),
                                  void* data, bool islocal);

namespace awtrix::script {
namespace {

// The VM fires the observability hook every 2^(SAMPLING - 1) instructions -- one less than
// upstream's comment claims; the mask in be_vm.c is ((1 << (SAMPLING - 1)) - 1).
constexpr long kHeartbeatPeriod = 1L << (BE_VM_OBSERVABILITY_SAMPLING - 1);

long g_budget = 0;

const char* const kLimitMsg = "instruction limit exceeded";

enum AbortStage {
  kArmed = 0,
  kDrained,
  kHardAborted,
};
int g_stage = kArmed;

bool g_mallocFailed = false;

// Two-stage abort: overrunning the budget first raises a catchable Berry exception, and a
// script that swallows it is be_throw()n out on the next heartbeat, which nothing can catch.
void budgetHook(bvm* vm, int event, ...) {
  if (event == BE_OBS_MALLOC_FAIL) {
    g_mallocFailed = true;
    return;
  }
  if (event != BE_OBS_VM_HEARTBEAT) return;

  if (g_stage >= kDrained) {
    g_stage = kHardAborted;
    be_throw(vm, BE_EXEC_ERROR);
    return;
  }

  g_budget -= kHeartbeatPeriod;
  if (g_budget <= 0) {
    g_stage = kDrained;
    be_raise(vm, "runtime_error", kLimitMsg);
  }
}

void armBudget() {
  g_budget = BerryVM::kInstructionLimit;
  g_stage = kArmed;
  g_mallocFailed = false;
}

struct SourceBuf {
  const char* text;
  size_t len;
};

// Hands the whole source to be_protectedparser in one go, then reports EOF by returning
// nullptr on the second call.
const char* readSourceOnce(struct blexer*, void* data, size_t* size) {
  auto* buf = static_cast<SourceBuf*>(data);
  *size = buf->len;
  buf->len = 0;
  return *size ? buf->text : nullptr;
}

}

BerryVM::BerryVM() {
  vm_ = be_vm_new();
  if (!vm_) return;

  be_set_obs_hook(vm_, budgetHook);

  // Collect once the heap grows 10% past live usage, against a stock 100%. Every app shares
  // this one heap on a device with ~96 KB for it, so a tight cycle beats GC throughput.
  be_gc_setsteprate(vm_, 110);

  // Sandboxing: these reach the filesystem and stdin, and no script may have them.
  static const char* const kClosedBuiltins[] = {
      "open",
      "input",
  };
  for (const char* name : kClosedBuiltins) {
    be_pushnil(vm_);
    be_setglobal(vm_, name);
    be_pop(vm_, 1);
  }

  bootstrapErr_ = "prelude not loaded";
}

BerryVM::~BerryVM() {
  if (vm_) be_vm_delete(vm_);
}

std::size_t BerryVM::heapBytes() const {
  return vm_ ? be_gc_memcount(vm_) : 0;
}

// Turns a be_pcall/parser return code into err_ and leaves the stack empty either way.
// A failed call left the exception type and message in the top two slots.
bool BerryVM::captureError(int rc) {
  if (rc == BE_OK) {
    err_.clear();
    return true;
  }
  err_.clear();
  if (be_top(vm_) >= 2) {
    const char* type = be_tostring(vm_, -2);
    const char* msg = be_tostring(vm_, -1);
    if (type) err_ = type;
    if (msg && *msg) {
      if (!err_.empty()) err_ += ": ";
      err_ += msg;
    }
  }
  if (g_stage == kHardAborted) err_ = kLimitMsg;
  if (g_mallocFailed) err_ = "out of memory";
  if (err_.empty()) err_ = "script error";
  be_pop(vm_, be_top(vm_));
  return false;
}

bool BerryVM::load(const std::string& source) {
  if (!vm_) {
    err_ = "vm alloc failed";
    return false;
  }
  armBudget();
  int rc = be_loadbuffer(vm_, "script", source.c_str(), source.size());
  if (!captureError(rc)) return false;

  rc = be_pcall(vm_, 0);
  bool ok = captureError(rc);
  if (ok) be_pop(vm_, be_top(vm_));
  return ok;
}

bool BerryVM::loadSolidifiedPrelude() {
  if (!vm_) {
    err_ = "vm alloc failed";
    return false;
  }
  armBudget();
  awtrix_push_solidified_prelude(vm_);
  int rc = be_pcall(vm_, 0);
  bool ok = captureError(rc);
  if (ok) be_pop(vm_, be_top(vm_));
  bootstrapErr_ = ok ? std::string()
                     : (err_.empty() ? "app registry bootstrap failed" : err_);
  return ok;
}

bool BerryVM::loadApp(const std::string& appKey, const std::string& source,
                      const char* const* methods, int methodCount,
                      uint32_t& implemented) {
  implemented = 0;
  if (!vm_) {
    err_ = "vm alloc failed";
    return false;
  }
  if (!bootstrapErr_.empty()) {
    err_ = bootstrapErr_;
    return false;
  }

  armBudget();
  SourceBuf buf{source.c_str(), source.size()};
  // islocal = true: top-level `var` in the script becomes a chunk local instead of a VM
  // global. Every app shares one VM, so without it two scripts would collide on names.
  int rc = be_protectedparser(vm_, "script", readSourceOnce, &buf, true);
  if (!captureError(rc)) return false;

  rc = be_pcall(vm_, 0);
  if (!captureError(rc)) return false;

  if (be_top(vm_) < 1 || !be_isinstance(vm_, -1)) {
    be_pop(vm_, be_top(vm_));
    err_ = "script must end with 'return YourApp()'";
    return false;
  }

  for (int i = 0; i < methodCount && i < 32; ++i) {
    if (be_getmethod(vm_, 1, methods[i])) implemented |= (1u << i);
    be_pop(vm_, 1);
  }

  // Files the instance in the prelude's _apps map. It is the only reference the VM keeps,
  // so without this the GC would reclaim the app the moment the stack unwinds.
  be_getglobal(vm_, "_app_anchor");
  be_pushstring(vm_, appKey.c_str());
  be_pushvalue(vm_, 1);
  rc = be_pcall(vm_, 2);
  bool ok = captureError(rc);
  if (ok) be_pop(vm_, be_top(vm_));
  return ok;
}

bool BerryVM::loadModule(const std::string& importName, const std::string& source) {
  if (!vm_) {
    err_ = "vm alloc failed";
    return false;
  }
  if (!bootstrapErr_.empty()) {
    err_ = bootstrapErr_;
    return false;
  }

  armBudget();
  SourceBuf buf{source.c_str(), source.size()};
  int rc = be_protectedparser(vm_, "script", readSourceOnce, &buf, true);
  if (!captureError(rc)) return false;

  rc = be_pcall(vm_, 0);
  if (!captureError(rc)) return false;

  if (be_top(vm_) < 1 || be_isnil(vm_, -1)) {
    be_pop(vm_, be_top(vm_));
    err_ = "module must end with 'return <value>'";
    return false;
  }

  awtrix_module_cache_set(vm_, importName.c_str());
  be_pop(vm_, be_top(vm_));
  return true;
}

void BerryVM::dropModule(const std::string& importName) {
  if (vm_) awtrix_module_cache_drop(vm_, importName.c_str());
}

// Calls appKey's `name` method and reads back at most one of out/boolOut/intOut. Slots are
// tracked absolutely from `base` so every exit path can unwind the stack to where it began.
bool BerryVM::doMethod(const std::string& appKey, const char* name, int argc,
                       const std::string* a, std::string* out, bool* boolOut,
                       long* intOut) {
  if (!vm_) {
    err_ = "vm alloc failed";
    return false;
  }

  const int base = be_top(vm_);

  const int instSlot = base + 1;
  be_getglobal(vm_, "_app_instance");
  be_pushstring(vm_, appKey.c_str());
  int rc = be_pcall(vm_, 1);
  if (!captureError(rc)) return false;
  be_pop(vm_, be_top(vm_) - instSlot);
  if (!be_isinstance(vm_, instSlot)) {
    be_pop(vm_, be_top(vm_) - base);
    err_ = "app not loaded";
    return false;
  }

  const int methodSlot = instSlot + 1;
  if (!be_getmethod(vm_, instSlot, name)) {
    be_pop(vm_, be_top(vm_) - base);
    err_ = std::string(name) + " is not a method";
    return false;
  }
  be_pushvalue(vm_, instSlot);
  if (argc >= 1) be_pushstring(vm_, a->c_str());

  armBudget();
  rc = be_pcall(vm_, 1 + argc);
  bool ok = captureError(rc);
  if (ok) {
    const bool haveResult = be_top(vm_) >= methodSlot;
    if (out) {
      const char* s = haveResult ? be_tostring(vm_, methodSlot) : nullptr;
      out->assign(s ? s : "");
    }
    if (boolOut && haveResult && !be_isnil(vm_, methodSlot))
      *boolOut = be_tobool(vm_, methodSlot) != 0;
    if (intOut && haveResult) {
      if (be_isint(vm_, methodSlot))
        *intOut = static_cast<long>(be_toint(vm_, methodSlot));
      else if (be_isreal(vm_, methodSlot))
        *intOut = static_cast<long>(be_toreal(vm_, methodSlot));
    }
    be_pop(vm_, be_top(vm_) - base);
  }
  return ok;
}

bool BerryVM::method(const std::string& appKey, const char* name) {
  return doMethod(appKey, name, 0, nullptr);
}

bool BerryVM::method1(const std::string& appKey, const char* name,
                      const std::string& a) {
  return doMethod(appKey, name, 1, &a);
}

bool BerryVM::method1Bool(const std::string& appKey, const char* name,
                          const std::string& a, bool& out) {
  return doMethod(appKey, name, 1, &a, nullptr, &out);
}

bool BerryVM::methodString(const std::string& appKey, const char* name,
                           std::string& out) {
  return doMethod(appKey, name, 0, nullptr, &out);
}

bool BerryVM::methodBool(const std::string& appKey, const char* name, bool& out) {
  return doMethod(appKey, name, 0, nullptr, nullptr, &out);
}

bool BerryVM::methodInt(const std::string& appKey, const char* name, long& out) {
  return doMethod(appKey, name, 0, nullptr, nullptr, nullptr, &out);
}

bool BerryVM::dropApp(const std::string& appKey) {
  if (!vm_ || !bootstrapErr_.empty()) return false;
  return call1("_app_drop", appKey);
}

void BerryVM::gcCollect() {
  if (vm_) be_gc_collect(vm_);
}

bool BerryVM::hasFunction(const char* name) const {
  if (!vm_) return false;
  bvm* vm = const_cast<bvm*>(vm_);
  be_getglobal(vm, name);
  bool ok = be_isfunction(vm, -1);
  be_pop(vm, 1);
  return ok;
}

bool BerryVM::doCall(const char* name, int argc, const std::string* a,
                     const std::string* b, const std::string* c, std::string* out) {
  if (!vm_) {
    err_ = "vm alloc failed";
    return false;
  }
  be_getglobal(vm_, name);
  if (!be_isfunction(vm_, -1)) {
    be_pop(vm_, 1);
    err_ = std::string(name) + " is not a function";
    return false;
  }
  if (argc >= 1) be_pushstring(vm_, a->c_str());
  if (argc >= 2) be_pushstring(vm_, b->c_str());
  if (argc >= 3) be_pushstring(vm_, c->c_str());

  armBudget();
  int rc = be_pcall(vm_, argc);
  bool ok = captureError(rc);
  if (ok) {
    if (out) {
      const char* s = be_top(vm_) >= 1 ? be_tostring(vm_, -1) : nullptr;
      out->assign(s ? s : "");
    }
    be_pop(vm_, be_top(vm_));
  }
  return ok;
}

bool BerryVM::call(const char* n) { return doCall(n, 0, nullptr, nullptr, nullptr); }

bool BerryVM::call1(const char* n, const std::string& a) {
  return doCall(n, 1, &a, nullptr, nullptr);
}

bool BerryVM::call2(const char* n, const std::string& a, const std::string& b) {
  return doCall(n, 2, &a, &b, nullptr);
}

bool BerryVM::call3(const char* n, const std::string& a, const std::string& b,
                    const std::string& c) {
  return doCall(n, 3, &a, &b, &c);
}

bool BerryVM::callString(const char* n, std::string& out) {
  return doCall(n, 0, nullptr, nullptr, nullptr, &out);
}

}
