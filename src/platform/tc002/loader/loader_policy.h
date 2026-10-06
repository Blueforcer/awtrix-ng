#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOADER_ATTEMPT_LIMIT 3

enum loader_choice {
  LOADER_START_DAEMON = 0,
  LOADER_VENDOR_UPGRADE,
  LOADER_VENDOR_KNOB,
  LOADER_VENDOR_THIS_BOOT,
  LOADER_HOLD_DEPLOY,
  LOADER_RESCUE_NO_RELEASE,
  LOADER_RESCUE_DAMAGED_RELEASE,
  LOADER_RESCUE_UNHEALTHY,
};

/* The release slot in res as the loader found it. */
enum loader_release {
  LOADER_RELEASE_MISSING = 0,
  LOADER_RELEASE_DAMAGED,
  LOADER_RELEASE_VALID,
};

struct loader_facts {
  int upgrade_pending;
  int knob_held;
  int vendor_this_boot;
  int deploy_active;
  enum loader_release release;
  int boot_attempts;
};

enum loader_choice loader_decide(const struct loader_facts* facts);
const char* loader_choice_reason(enum loader_choice choice);
const char* loader_release_name(enum loader_release release);
int loader_choice_resets_attempts(enum loader_choice choice);
int loader_choice_sticks_this_boot(enum loader_choice choice);
int loader_choice_rescues(enum loader_choice choice);

int loader_parse_attempts(const char* text, size_t length);
int loader_combine_attempts(int durable, int this_boot);
int loader_upgrade_requested(const char* flag);
const char* loader_upgrade_dir(const char* sys_dir, const char* persist_dir,
                               const char* default_dir);

#ifdef __cplusplus
}
#endif
