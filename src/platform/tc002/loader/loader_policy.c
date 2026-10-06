#include "loader_policy.h"

#include <string.h>

static int attempts_left(int attempts) {
  return attempts >= 0 && attempts < LOADER_ATTEMPT_LIMIT;
}

enum loader_choice loader_decide(const struct loader_facts* facts) {
  if (facts->upgrade_pending) return LOADER_VENDOR_UPGRADE;
  if (facts->knob_held) return LOADER_VENDOR_KNOB;
  if (facts->vendor_this_boot) return LOADER_VENDOR_THIS_BOOT;
  if (facts->deploy_active) return LOADER_HOLD_DEPLOY;
  if (facts->release == LOADER_RELEASE_MISSING) return LOADER_RESCUE_NO_RELEASE;
  if (facts->release == LOADER_RELEASE_DAMAGED) return LOADER_RESCUE_DAMAGED_RELEASE;
  if (!attempts_left(facts->boot_attempts)) return LOADER_RESCUE_UNHEALTHY;
  return LOADER_START_DAEMON;
}

const char* loader_choice_reason(enum loader_choice choice) {
  switch (choice) {
    case LOADER_START_DAEMON: return "starting awtrix-tc002d";
    case LOADER_VENDOR_UPGRADE: return "vendor update pending: vendor app runs the flasher";
    case LOADER_VENDOR_KNOB: return "knob held at power-on: vendor app";
    case LOADER_VENDOR_THIS_BOOT: return "vendor app already chosen in this boot";
    case LOADER_HOLD_DEPLOY: return "USB deploy in progress: waiting for the installer";
    case LOADER_RESCUE_NO_RELEASE: return "no AWTRIX release in res: rescue";
    case LOADER_RESCUE_DAMAGED_RELEASE: return "the AWTRIX release in res is damaged: rescue";
    case LOADER_RESCUE_UNHEALTHY: return "the last starts never became healthy: rescue";
  }
  return "unknown";
}

const char* loader_release_name(enum loader_release release) {
  switch (release) {
    case LOADER_RELEASE_MISSING: return "missing";
    case LOADER_RELEASE_DAMAGED: return "damaged";
    case LOADER_RELEASE_VALID: return "valid";
  }
  return "unknown";
}

/* A rescue after unhealthy starts clears the counter; the next power-on retries the release. */
int loader_choice_resets_attempts(enum loader_choice choice) {
  return choice == LOADER_VENDOR_KNOB || choice == LOADER_RESCUE_UNHEALTHY;
}

int loader_choice_sticks_this_boot(enum loader_choice choice) {
  return choice == LOADER_VENDOR_KNOB;
}

int loader_choice_rescues(enum loader_choice choice) {
  return choice == LOADER_RESCUE_NO_RELEASE || choice == LOADER_RESCUE_DAMAGED_RELEASE ||
         choice == LOADER_RESCUE_UNHEALTHY;
}

int loader_parse_attempts(const char* text, size_t length) {
  size_t i = 0;
  int value = 0;
  if (!length) return -1;
  for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i) {
    if (value > 1000) return -1;
    value = value * 10 + (text[i] - '0');
  }
  if (!i || value > 1000) return -1;
  if (i < length && text[i] == '\n') ++i;
  return i == length ? value : -1;
}

int loader_combine_attempts(int durable, int this_boot) {
  if (durable < 0 || this_boot < 0) return -1;
  return durable > this_boot ? durable : this_boot;
}

int loader_upgrade_requested(const char* flag) {
  return flag && *flag && strcmp(flag, "0") != 0;
}

const char* loader_upgrade_dir(const char* sys_dir, const char* persist_dir,
                               const char* default_dir) {
  if (sys_dir && *sys_dir) return sys_dir;
  if (persist_dir && *persist_dir) return persist_dir;
  return default_dir;
}
