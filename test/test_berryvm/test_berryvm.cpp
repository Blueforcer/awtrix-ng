
#include <unity.h>

#include <stdio.h>

#include <string>

#include <string.h>

#include "berry.h"

#include "core/script/BerryVM.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

static void test_berry_runs_arithmetic() {
  bvm* vm = be_vm_new();
  TEST_ASSERT_NOT_NULL(vm);

  TEST_ASSERT_EQUAL_INT(BE_OK, be_loadstring(vm, "def f() return 1 + 2 end"));
  TEST_ASSERT_EQUAL_INT(BE_OK, be_pcall(vm, 0));
  TEST_ASSERT_EQUAL_INT(1, be_top(vm));
  be_pop(vm, 1);

  be_getglobal(vm, "f");
  TEST_ASSERT_EQUAL_INT(BE_OK, be_pcall(vm, 0));
  TEST_ASSERT_TRUE(be_isint(vm, -1));
  TEST_ASSERT_EQUAL_INT(3, be_toint(vm, -1));
  be_pop(vm, 1);

  be_vm_delete(vm);
}

static int g_heartbeats = 0;

static void count_heartbeats(bvm* vm, int event, ...) {
  (void)vm;
  if (event == BE_OBS_VM_HEARTBEAT) {
    ++g_heartbeats;
  }
}

static void test_observability_heartbeat_fires() {
  g_heartbeats = 0;

  bvm* vm = be_vm_new();
  TEST_ASSERT_NOT_NULL(vm);
  be_set_obs_hook(vm, count_heartbeats);

  TEST_ASSERT_EQUAL_INT(
      BE_OK,
      be_loadstring(vm, "var i = 0 while i < 20000 i = i + 1 end"));
  TEST_ASSERT_EQUAL_INT(BE_OK, be_pcall(vm, 0));
  be_pop(vm, 1);

  TEST_ASSERT_GREATER_THAN_INT(1, g_heartbeats);

  be_vm_delete(vm);
}

static void test_sandboxed_modules_are_absent() {
  bvm* vm = be_vm_new();
  TEST_ASSERT_NOT_NULL(vm);

  TEST_ASSERT_EQUAL_INT(BE_OK, be_loadstring(vm, "import os"));
  TEST_ASSERT_EQUAL_INT(BE_EXCEPTION, be_pcall(vm, 0));

  TEST_ASSERT_EQUAL_INT(3, be_top(vm));
  TEST_ASSERT_EQUAL_STRING("import_error", be_tostring(vm, -2));
  TEST_ASSERT_EQUAL_STRING("module 'os' not found", be_tostring(vm, -1));
  be_pop(vm, 3);

  be_vm_delete(vm);
}

static void test_raw_vm_still_exposes_open_builtin() {
  bvm* vm = be_vm_new();
  TEST_ASSERT_NOT_NULL(vm);

  TEST_ASSERT_TRUE(be_getglobal(vm, "open"));
  TEST_ASSERT_TRUE(be_isfunction(vm, -1));
  be_pop(vm, 1);

  be_vm_delete(vm);
}


static void expectImportable(const char* name, bool allowed) {
  script::BerryVM vm;
  std::string src = std::string("def probe() import ") + name + " end";
  TEST_ASSERT_TRUE_MESSAGE(vm.load(src), name);
  const bool got = vm.call("probe");
  if (got != allowed) {
    std::string msg = std::string("module '") + name + "' is " +
                      (got ? "REACHABLE but should be sandboxed"
                           : "unreachable but should be available");
    TEST_FAIL_MESSAGE(msg.c_str());
  }
}

static void test_vm_module_sandbox_inventory() {
  expectImportable("string", true);
  expectImportable("json", true);
  expectImportable("math", true);
  expectImportable("gc", true);
  expectImportable("strict", true);
  expectImportable("global", true);

  expectImportable("os", false);
  expectImportable("sys", false);
  expectImportable("time", false);
  expectImportable("debug", false);
  expectImportable("solidify", false);
  expectImportable("introspect", false);
  expectImportable("path", false);
}

static void test_vm_introspect_module_is_unreachable() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load("def t() import introspect return str(introspect.fromptr(4)) end"));
  TEST_ASSERT_FALSE(vm.call("t"));
  TEST_ASSERT_TRUE(vm.lastError().find("introspect") != std::string::npos);
}

static void test_vm_input_builtin_is_unreachable() {
  script::BerryVM vm;
  TEST_ASSERT_FALSE(vm.hasFunction("input"));
  TEST_ASSERT_TRUE(vm.load("def ask() return input() end"));
  TEST_ASSERT_FALSE(vm.call("ask"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
}

static void test_vm_nil_shadowed_builtins_have_no_back_door() {
  script::BerryVM vm;
  std::string out;

  TEST_ASSERT_TRUE(vm.load("def m1() import global return str(global.member('open')) end"));
  TEST_ASSERT_TRUE(vm.callString("m1", out));
  TEST_ASSERT_EQUAL_STRING("nil", out.c_str());

  TEST_ASSERT_TRUE(vm.load("def m2() import global return str(global.member('input')) end"));
  TEST_ASSERT_TRUE(vm.callString("m2", out));
  TEST_ASSERT_EQUAL_STRING("nil", out.c_str());

  TEST_ASSERT_TRUE(vm.load("def m3() return str(compile('return open')()) end"));
  TEST_ASSERT_TRUE(vm.callString("m3", out));
  TEST_ASSERT_EQUAL_STRING("nil", out.c_str());

  TEST_ASSERT_TRUE(vm.load("def m4() return str(compile('return input')()) end"));
  TEST_ASSERT_TRUE(vm.callString("m4", out));
  TEST_ASSERT_EQUAL_STRING("nil", out.c_str());
}


static void test_vm_load_and_call() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load("var n = 0\ndef draw() n = n + 1 end"));
  TEST_ASSERT_TRUE(vm.hasFunction("draw"));
  TEST_ASSERT_TRUE(vm.call("draw"));
  TEST_ASSERT_EQUAL_STRING("", vm.lastError().c_str());
}

static void test_vm_syntax_error_captured() {
  script::BerryVM vm;
  TEST_ASSERT_FALSE(vm.load("def draw( broken"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
}

static void test_vm_runtime_error_captured() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load("def draw() var f = nil f() end"));
  TEST_ASSERT_FALSE(vm.call("draw"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
}

static void test_vm_undeclared_global_is_a_compile_error() {
  script::BerryVM vm;
  TEST_ASSERT_FALSE(vm.load("def draw() undefined_fn() end"));
  TEST_ASSERT_EQUAL_STRING(
      "syntax_error: script:1: 'undefined_fn' undeclared (first use in this function)",
      vm.lastError().c_str());
}

static void test_vm_infinite_loop_hits_instruction_limit() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load("def draw() while true end end"));
  TEST_ASSERT_FALSE(vm.call("draw"));
  TEST_ASSERT_TRUE(vm.lastError().find("instruction") != std::string::npos);
}

static void test_vm_budget_resets_between_calls() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load(
      "def spin() while true end end\n"
      "def work() var i = 0 while i < 5000 i = i + 1 end return i end"));
  TEST_ASSERT_FALSE(vm.call("spin"));
  TEST_ASSERT_TRUE(vm.call("work"));
  TEST_ASSERT_TRUE(vm.call("work"));
  TEST_ASSERT_EQUAL_STRING("", vm.lastError().c_str());
}

static void test_vm_instruction_limit_survives_try_except() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load(
      "var n = 0\n"
      "def spin()\n"
      "  while true\n"
      "    try\n"
      "      while true n += 1 end\n"
      "    except .. as e, m\n"
      "    end\n"
      "  end\n"
      "end"));
  TEST_ASSERT_FALSE(vm.call("spin"));
  TEST_ASSERT_TRUE(vm.lastError().find("instruction") != std::string::npos);
}

static void test_vm_stays_usable_after_hard_abort() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load(
      "def spin()\n"
      "  while true\n"
      "    try while true end except .. end\n"
      "  end\n"
      "end\n"
      "def work() var i = 0 while i < 5000 i = i + 1 end return i end\n"
      "def caught()\n"
      "  try\n"
      "    raise 'boom'\n"
      "  except .. as e\n"
      "    return 'caught:' + e\n"
      "  end\n"
      "end"));
  TEST_ASSERT_FALSE(vm.call("spin"));
  TEST_ASSERT_TRUE(vm.call("work"));

  std::string out;
  TEST_ASSERT_TRUE(vm.callString("caught", out));
  TEST_ASSERT_EQUAL_STRING("caught:boom", out.c_str());
}

static void test_vm_instruction_limit_applies_to_load() {
  script::BerryVM vm;
  TEST_ASSERT_FALSE(vm.load("while true end"));
  TEST_ASSERT_TRUE(vm.lastError().find("instruction") != std::string::npos);
}

static void test_vm_call_with_string_args() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load(
      "var got = ''\ndef f(a, b) got = a + b end\ndef check() return got end"));
  TEST_ASSERT_TRUE(vm.call2("f", "x", "y"));
  TEST_ASSERT_TRUE(vm.call("check"));

  TEST_ASSERT_FALSE(vm.call1("f", "z"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
}

static void test_vm_missing_function_is_an_error_not_a_crash() {
  script::BerryVM vm;
  TEST_ASSERT_TRUE(vm.load("def draw() end"));
  TEST_ASSERT_FALSE(vm.hasFunction("on_show"));
  TEST_ASSERT_FALSE(vm.call("on_show"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
}

static void test_vm_open_builtin_is_unreachable() {
  script::BerryVM vm;
  TEST_ASSERT_FALSE(vm.hasFunction("open"));
  TEST_ASSERT_TRUE(vm.load("def touch() open('berryvm_sandbox_breach.txt', 'w') end"));
  TEST_ASSERT_FALSE(vm.call("touch"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);

  FILE* f = fopen("berryvm_sandbox_breach.txt", "r");
  if (f != NULL) {
    fclose(f);
    TEST_FAIL_MESSAGE("Berry `open` builtin created a real file: sandbox breached");
  }
}


static const char* const kHooks[] = {"draw", "loop", "get"};
static constexpr int kHookCount = 3;

static void primeAppRegistry(script::BerryVM& vm) {
  TEST_ASSERT_TRUE_MESSAGE(vm.loadSolidifiedPrelude(), vm.lastError().c_str());
}

static void test_vm_loadapp_refused_without_prelude() {
  script::BerryVM vm;
  uint32_t hooks = 0;
  TEST_ASSERT_FALSE(vm.loadApp("a", "return 1", kHooks, kHookCount, hooks));
  TEST_ASSERT_TRUE(vm.lastError().find("prelude") != std::string::npos);
}

static void test_vm_loadapp_and_method() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  bool loaded = vm.loadApp("counter",
                           "class Counter\n"
                           "  var n\n"
                           "  def init() self.n = 0 end\n"
                           "  def draw() self.n += 1 end\n"
                           "  def get() return self.n end\n"
                           "end\n"
                           "return Counter()",
                           kHooks, kHookCount, hooks);
  TEST_ASSERT_TRUE_MESSAGE(loaded, vm.lastError().c_str());
  TEST_ASSERT_EQUAL_UINT32((1u << 0) | (1u << 2), hooks);

  TEST_ASSERT_TRUE(vm.method("counter", "draw"));
  TEST_ASSERT_TRUE(vm.method("counter", "draw"));
  std::string out;
  TEST_ASSERT_TRUE(vm.methodString("counter", "get", out));
  TEST_ASSERT_EQUAL_STRING("2", out.c_str());
}

static void test_vm_loadapp_requires_instance_return() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_FALSE(vm.loadApp("a", "class A def draw() end end", kHooks,
                               kHookCount, hooks));
  TEST_ASSERT_TRUE(vm.lastError().find("return") != std::string::npos);
  TEST_ASSERT_FALSE(vm.loadApp("a", "return 42", kHooks, kHookCount, hooks));
  TEST_ASSERT_TRUE(vm.lastError().find("return") != std::string::npos);
  TEST_ASSERT_FALSE(vm.method("a", "draw"));
}

static void test_vm_loadapp_same_class_name_is_isolated() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_TRUE(vm.loadApp("one",
                              "class App\n"
                              "  var v\n"
                              "  def init() self.v = 'one' end\n"
                              "  def get() return self.v end\n"
                              "end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  TEST_ASSERT_TRUE(vm.loadApp("two",
                              "class App\n"
                              "  var v\n"
                              "  def init() self.v = 'two' end\n"
                              "  def get() return self.v end\n"
                              "end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  std::string out;
  TEST_ASSERT_TRUE(vm.methodString("one", "get", out));
  TEST_ASSERT_EQUAL_STRING("one", out.c_str());
  TEST_ASSERT_TRUE(vm.methodString("two", "get", out));
  TEST_ASSERT_EQUAL_STRING("two", out.c_str());
  TEST_ASSERT_FALSE(vm.load("return App"));
}

static void test_vm_loadapp_anchor_survives_gc() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_TRUE(vm.loadApp("keep",
                              "class App\n"
                              "  var v\n"
                              "  def init() self.v = 7 end\n"
                              "  def get() return self.v end\n"
                              "end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  vm.gcCollect();
  std::string out;
  TEST_ASSERT_TRUE(vm.methodString("keep", "get", out));
  TEST_ASSERT_EQUAL_STRING("7", out.c_str());
}

static void test_vm_method_failure_leaves_stack_balanced() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_TRUE(vm.loadApp("bad",
                              "class App\n"
                              "  def draw() raise 'oops' end\n"
                              "  def get() return 'alive' end\n"
                              "end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  TEST_ASSERT_FALSE(vm.method("bad", "draw"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
  TEST_ASSERT_EQUAL_INT(0, be_top(vm.raw()));
  std::string out;
  TEST_ASSERT_TRUE(vm.methodString("bad", "get", out));
  TEST_ASSERT_EQUAL_STRING("alive", out.c_str());
}

static void test_vm_method_instruction_limit() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_TRUE(vm.loadApp("spin",
                              "class App\n"
                              "  def draw() while true end end\n"
                              "end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  TEST_ASSERT_FALSE(vm.method("spin", "draw"));
  TEST_ASSERT_TRUE(vm.lastError().find("instruction") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(0, be_top(vm.raw()));
}

static void test_vm_loadapp_reload_replaces_instance() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_TRUE(vm.loadApp("app",
                              "class App def get() return 'old' end end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  TEST_ASSERT_TRUE(vm.loadApp("app",
                              "class App def get() return 'new' end end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  std::string out;
  TEST_ASSERT_TRUE(vm.methodString("app", "get", out));
  TEST_ASSERT_EQUAL_STRING("new", out.c_str());
}

static void test_vm_method_on_unknown_app_is_an_error() {
  script::BerryVM vm;
  TEST_ASSERT_FALSE(vm.method("ghost", "draw"));
  TEST_ASSERT_TRUE(vm.lastError().size() > 0);
  TEST_ASSERT_EQUAL_INT(0, be_top(vm.raw()));
}

static void test_vm_dropapp_unloads() {
  script::BerryVM vm;
  primeAppRegistry(vm);
  uint32_t hooks = 0;
  TEST_ASSERT_TRUE(vm.loadApp("app",
                              "class App def get() return 'here' end end\n"
                              "return App()",
                              kHooks, kHookCount, hooks));
  std::string out;
  TEST_ASSERT_TRUE(vm.methodString("app", "get", out));
  TEST_ASSERT_EQUAL_STRING("here", out.c_str());

  TEST_ASSERT_TRUE(vm.dropApp("app"));
  vm.gcCollect();
  TEST_ASSERT_FALSE(vm.method("app", "get"));
  TEST_ASSERT_TRUE(vm.dropApp("app"));
  TEST_ASSERT_EQUAL_INT(0, be_top(vm.raw()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_berry_runs_arithmetic);
  RUN_TEST(test_observability_heartbeat_fires);
  RUN_TEST(test_sandboxed_modules_are_absent);
  RUN_TEST(test_raw_vm_still_exposes_open_builtin);
  RUN_TEST(test_vm_module_sandbox_inventory);
  RUN_TEST(test_vm_introspect_module_is_unreachable);
  RUN_TEST(test_vm_input_builtin_is_unreachable);
  RUN_TEST(test_vm_nil_shadowed_builtins_have_no_back_door);
  RUN_TEST(test_vm_load_and_call);
  RUN_TEST(test_vm_syntax_error_captured);
  RUN_TEST(test_vm_runtime_error_captured);
  RUN_TEST(test_vm_undeclared_global_is_a_compile_error);
  RUN_TEST(test_vm_infinite_loop_hits_instruction_limit);
  RUN_TEST(test_vm_budget_resets_between_calls);
  RUN_TEST(test_vm_instruction_limit_survives_try_except);
  RUN_TEST(test_vm_stays_usable_after_hard_abort);
  RUN_TEST(test_vm_instruction_limit_applies_to_load);
  RUN_TEST(test_vm_call_with_string_args);
  RUN_TEST(test_vm_missing_function_is_an_error_not_a_crash);
  RUN_TEST(test_vm_open_builtin_is_unreachable);
  RUN_TEST(test_vm_loadapp_refused_without_prelude);
  RUN_TEST(test_vm_loadapp_and_method);
  RUN_TEST(test_vm_loadapp_requires_instance_return);
  RUN_TEST(test_vm_loadapp_same_class_name_is_isolated);
  RUN_TEST(test_vm_loadapp_anchor_survives_gc);
  RUN_TEST(test_vm_method_failure_leaves_stack_balanced);
  RUN_TEST(test_vm_method_instruction_limit);
  RUN_TEST(test_vm_loadapp_reload_replaces_instance);
  RUN_TEST(test_vm_method_on_unknown_app_is_an_error);
  RUN_TEST(test_vm_dropapp_unloads);
  return UNITY_END();
}
