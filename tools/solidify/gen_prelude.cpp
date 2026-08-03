
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "berry.h"
#include "be_vm.h"
}

extern "C" {
void* awtrix_script_heap_alloc(size_t size) { return std::malloc(size); }
void* awtrix_script_heap_realloc(void* ptr, size_t size) { return std::realloc(ptr, size); }
void awtrix_script_heap_free(void* ptr) { std::free(ptr); }
}

static int noop(bvm* vm) { be_return_nil(vm); }

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: gen_prelude <natives-file> < prelude.be\n");
    return 2;
  }

  std::vector<std::string> natives;
  {
    std::ifstream f(argv[1]);
    if (!f) {
      std::fprintf(stderr, "gen_prelude: cannot open %s\n", argv[1]);
      return 2;
    }
    std::string line;
    while (std::getline(f, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
      if (!line.empty()) natives.push_back(line);
    }
  }

  std::string src;
  {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0) src.append(buf, n);
  }
  if (src.empty()) {
    std::fprintf(stderr, "gen_prelude: empty source on stdin\n");
    return 2;
  }

  bvm* vm = be_vm_new();
  if (!vm) {
    std::fprintf(stderr, "gen_prelude: be_vm_new failed\n");
    return 2;
  }

  comp_set_named_gbl(vm);

  for (const std::string& name : natives) be_regfunc(vm, name.c_str(), noop);

  int rc = be_loadbuffer(vm, "awtrix_prelude", src.c_str(), src.size());
  if (rc != BE_OK) {
    const char* err = be_tostring(vm, -1);
    std::fprintf(stderr, "gen_prelude: compile failed: %s\n", err ? err : "unknown");
    be_vm_delete(vm);
    return 1;
  }
  be_setglobal(vm, "PRELUDE_FN");
  be_pop(vm, 1);

  rc = be_loadstring(vm, "import solidify\n");
  if (rc == BE_OK) rc = be_pcall(vm, 0);
  if (rc != BE_OK) {
    const char* err = be_tostring(vm, -1);
    std::fprintf(stderr, "gen_prelude: import failed: %s\n", err ? err : "unknown");
    be_vm_delete(vm);
    return 1;
  }
  be_pop(vm, be_top(vm));

  rc = be_loadstring(vm, "solidify.dump(PRELUDE_FN, false)\n");
  if (rc == BE_OK) rc = be_pcall(vm, 0);
  if (rc != BE_OK) {
    const char* err = be_tostring(vm, -1);
    std::fprintf(stderr, "gen_prelude: dump failed: %s\n", err ? err : "unknown");
    be_vm_delete(vm);
    return 1;
  }

  be_vm_delete(vm);
  return 0;
}
