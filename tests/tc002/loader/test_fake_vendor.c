#include <stdio.h>

#ifndef AWTRIX_LOADER_TEST_ROOT
#error AWTRIX_LOADER_TEST_ROOT must name the harness directory
#endif

static void record(const char* event, void* context) {
  FILE* file = fopen(AWTRIX_LOADER_TEST_ROOT "/vendor.out", "a");
  if (!file) return;
  fprintf(file, "%s %p\n", event, context);
  fclose(file);
}

__attribute__((constructor)) static void constructed(void) { record("constructed", NULL); }

void onEasyUIInit(void* context) { record("init", context); }

void onEasyUIDeinit(void* context) { record("deinit", context); }

const char* onStartupApp(void* context) {
  record("startup", context);
  return "vendorActivity";
}
