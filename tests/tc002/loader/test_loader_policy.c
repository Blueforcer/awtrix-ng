#include "../../support.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/tc002/loader/loader_policy.h"
#include "platform/tc002/loader/loader_state.h"
#include "platform/tc002/contract/deploy_token_file.h"



#define CHECK(condition) AWTRIX_TEST_CHECK(condition, "loader policy")

static struct loader_facts healthy(void) {
  struct loader_facts facts = {.release = LOADER_RELEASE_VALID, .boot_attempts = 0};
  return facts;
}

static void test_decisions(void) {
  struct loader_facts facts = healthy();
  CHECK(loader_decide(&facts) == LOADER_START_DAEMON);
  facts.boot_attempts = LOADER_ATTEMPT_LIMIT - 1;
  CHECK(loader_decide(&facts) == LOADER_START_DAEMON);
  facts.boot_attempts = LOADER_ATTEMPT_LIMIT;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_UNHEALTHY);
  facts.boot_attempts = 99;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_UNHEALTHY);
  facts.boot_attempts = -1;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_UNHEALTHY);

  facts = healthy();
  facts.release = LOADER_RELEASE_MISSING;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_NO_RELEASE);
  facts.release = LOADER_RELEASE_DAMAGED;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_DAMAGED_RELEASE);
  facts.boot_attempts = 99;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_DAMAGED_RELEASE);
  facts.knob_held = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_KNOB);
  facts.knob_held = 0;
  facts.upgrade_pending = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_UPGRADE);

  facts = healthy();
  facts.vendor_this_boot = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_THIS_BOOT);
  facts.knob_held = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_KNOB);
  facts.upgrade_pending = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_UPGRADE);

  CHECK(!strcmp(loader_release_name(LOADER_RELEASE_MISSING), "missing"));
  CHECK(!strcmp(loader_release_name(LOADER_RELEASE_DAMAGED), "damaged"));
  CHECK(!strcmp(loader_release_name(LOADER_RELEASE_VALID), "valid"));

  const enum loader_choice rescues[] = {LOADER_RESCUE_NO_RELEASE, LOADER_RESCUE_DAMAGED_RELEASE,
                                        LOADER_RESCUE_UNHEALTHY};
  for (size_t i = 0; i < sizeof rescues / sizeof rescues[0]; ++i) {
    CHECK(loader_choice_rescues(rescues[i]));
    CHECK(!loader_choice_sticks_this_boot(rescues[i]));
  }
  CHECK(!loader_choice_resets_attempts(LOADER_RESCUE_NO_RELEASE));
  CHECK(!loader_choice_resets_attempts(LOADER_RESCUE_DAMAGED_RELEASE));
  CHECK(loader_choice_resets_attempts(LOADER_RESCUE_UNHEALTHY));
  CHECK(loader_choice_resets_attempts(LOADER_VENDOR_KNOB));
  CHECK(!loader_choice_resets_attempts(LOADER_VENDOR_UPGRADE));
  CHECK(!loader_choice_resets_attempts(LOADER_VENDOR_THIS_BOOT));
  CHECK(!loader_choice_resets_attempts(LOADER_START_DAEMON));
  CHECK(loader_choice_sticks_this_boot(LOADER_VENDOR_KNOB));
  CHECK(!loader_choice_sticks_this_boot(LOADER_VENDOR_UPGRADE));
  CHECK(!loader_choice_sticks_this_boot(LOADER_START_DAEMON));
  CHECK(!loader_choice_rescues(LOADER_START_DAEMON));
  CHECK(!loader_choice_rescues(LOADER_VENDOR_KNOB));
  CHECK(!loader_choice_rescues(LOADER_VENDOR_UPGRADE));
  CHECK(!loader_choice_rescues(LOADER_VENDOR_THIS_BOOT));
}

static void test_deploy_hold(void) {
  struct loader_facts facts = healthy();
  facts.deploy_active = 1;
  CHECK(loader_decide(&facts) == LOADER_HOLD_DEPLOY);
  CHECK(!loader_choice_rescues(LOADER_HOLD_DEPLOY));
  CHECK(!loader_choice_resets_attempts(LOADER_HOLD_DEPLOY));
  CHECK(!loader_choice_sticks_this_boot(LOADER_HOLD_DEPLOY));
  CHECK(!strcmp(loader_choice_reason(LOADER_HOLD_DEPLOY),
                "USB deploy in progress: waiting for the installer"));
  facts.release = LOADER_RELEASE_MISSING;
  CHECK(loader_decide(&facts) == LOADER_HOLD_DEPLOY);
  facts.deploy_active = 0;
  CHECK(loader_decide(&facts) == LOADER_RESCUE_NO_RELEASE);
  facts.release = LOADER_RELEASE_VALID;
  CHECK(loader_decide(&facts) == LOADER_START_DAEMON);

  facts = healthy();
  facts.deploy_active = 1;
  facts.knob_held = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_KNOB);
  facts.knob_held = 0;
  facts.upgrade_pending = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_UPGRADE);
  facts.upgrade_pending = 0;
  facts.vendor_this_boot = 1;
  CHECK(loader_decide(&facts) == LOADER_VENDOR_THIS_BOOT);
}

static int vendor_choice(enum loader_choice choice) {
  return choice != LOADER_START_DAEMON && choice != LOADER_HOLD_DEPLOY &&
         !loader_choice_rescues(choice);
}

static void test_vendor_only_on_request(void) {
  const int attempt_values[] = {-1, 0, 1, LOADER_ATTEMPT_LIMIT, 99};
  const enum loader_release releases[] = {LOADER_RELEASE_MISSING, LOADER_RELEASE_DAMAGED,
                                          LOADER_RELEASE_VALID};
  for (unsigned bits = 0; bits < 16; ++bits) {
    for (size_t r = 0; r < sizeof releases / sizeof releases[0]; ++r) {
      for (size_t a = 0; a < sizeof attempt_values / sizeof attempt_values[0]; ++a) {
        struct loader_facts facts = {
            .upgrade_pending = bits & 1,
            .knob_held = (bits >> 1) & 1,
            .vendor_this_boot = (bits >> 2) & 1,
            .deploy_active = (bits >> 3) & 1,
            .release = releases[r],
            .boot_attempts = attempt_values[a],
        };
        enum loader_choice choice = loader_decide(&facts);
        const int requested = facts.upgrade_pending || facts.knob_held || facts.vendor_this_boot;
        const int runnable = facts.release == LOADER_RELEASE_VALID && facts.boot_attempts >= 0 &&
                             facts.boot_attempts < LOADER_ATTEMPT_LIMIT;
        CHECK((choice == LOADER_HOLD_DEPLOY) == (facts.deploy_active && !requested));
        if (choice == LOADER_HOLD_DEPLOY) {
          facts.deploy_active = 0;
          choice = loader_decide(&facts);
        }
        CHECK(vendor_choice(choice) == requested);
        if (!requested) CHECK(runnable ? choice == LOADER_START_DAEMON : loader_choice_rescues(choice));
      }
    }
  }
}

static void test_parse_attempts(void) {
  CHECK(loader_parse_attempts("0", 1) == 0);
  CHECK(loader_parse_attempts("2\n", 2) == 2);
  CHECK(loader_parse_attempts("17", 2) == 17);
  CHECK(loader_parse_attempts("", 0) == -1);
  CHECK(loader_parse_attempts("\n", 1) == -1);
  CHECK(loader_parse_attempts("-1", 2) == -1);
  CHECK(loader_parse_attempts("3\n\n", 3) == -1);
  CHECK(loader_parse_attempts("3x", 2) == -1);
  CHECK(loader_parse_attempts("1001", 4) == -1);
  CHECK(loader_parse_attempts("99999999999999", 14) == -1);

  CHECK(loader_combine_attempts(0, 0) == 0);
  CHECK(loader_combine_attempts(2, 1) == 2);
  CHECK(loader_combine_attempts(1, 3) == 3);
  CHECK(loader_combine_attempts(-1, 0) == -1);
  CHECK(loader_combine_attempts(0, -1) == -1);
}

static void test_upgrade_inputs(void) {
  CHECK(!loader_upgrade_requested(NULL));
  CHECK(!loader_upgrade_requested(""));
  CHECK(!loader_upgrade_requested("0"));
  CHECK(loader_upgrade_requested("255"));
  CHECK(loader_upgrade_requested("1"));
  CHECK(!strcmp(loader_upgrade_dir("/data", "/x", "/mnt/storage"), "/data"));
  CHECK(!strcmp(loader_upgrade_dir("", "/x", "/mnt/storage"), "/x"));
  CHECK(!strcmp(loader_upgrade_dir("", "", "/mnt/storage"), "/mnt/storage"));
  CHECK(!strcmp(loader_upgrade_dir(NULL, NULL, "/mnt/storage"), "/mnt/storage"));
}

static void write_text(const char* path, const char* text) {
  FILE* file = fopen(path, "w");
  fputs(text, file);
  fclose(file);
}

static long file_size(const char* path) {
  struct stat info;
  return stat(path, &info) ? -1 : (long)info.st_size;
}

static void test_state_files(void) {
  char root[] = "/tmp/awtrix-loader-state-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char state[256], counter[256], log[256], previous[256], temporary[256];
  snprintf(state, sizeof state, "%s/state", root);
  snprintf(counter, sizeof counter, "%s/state/boot-attempts", root);
  snprintf(temporary, sizeof temporary, "%s/state/boot-attempts.new", root);
  snprintf(log, sizeof log, "%s/state/loader.log", root);
  snprintf(previous, sizeof previous, "%s/state/loader.log.1", root);

  CHECK(loader_ensure_directory(state, 0755) == 0);
  CHECK(loader_ensure_directory(state, 0755) == 0);
  CHECK(loader_read_attempts(counter) == 0);
  CHECK(loader_store_attempts(counter, 1) == 0);
  CHECK(loader_read_attempts(counter) == 1);
  CHECK(loader_store_attempts(counter, 2) == 0);
  CHECK(loader_read_attempts(counter) == 2);
  CHECK(access(temporary, F_OK) != 0);
  write_text(counter, "garbage");
  CHECK(loader_read_attempts(counter) == -1);
  write_text(counter, "");
  CHECK(loader_read_attempts(counter) == -1);
  CHECK(loader_clear_attempts(counter) == 0);
  CHECK(access(counter, F_OK) != 0);
  CHECK(loader_clear_attempts(counter) == 0);
  CHECK(loader_read_attempts(counter) == 0);

  char line[64];
  memset(line, 'x', sizeof line - 2);
  line[sizeof line - 2] = '\n';
  line[sizeof line - 1] = '\0';
  for (int i = 0; i < 10; ++i) CHECK(loader_append_log(log, line, 256, 1) == 0);
  CHECK(file_size(log) > 0 && file_size(log) <= 256);
  CHECK(file_size(previous) > 0 && file_size(previous) <= 256);
  CHECK(file_size(log) == 63 * 2 && file_size(previous) == 63 * 4);

  char file_path[256];
  snprintf(file_path, sizeof file_path, "%s/plain", root);
  write_text(file_path, "x");
  CHECK(loader_ensure_directory(file_path, 0755) < 0);

  unlink(file_path);
  unlink(log);
  unlink(previous);
  rmdir(state);
  rmdir(root);
}

static void test_deploy_token_files(void) {
  char root[] = "/tmp/awtrix-deploy-token-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char token[256], link[256], fifo[256], text[128];
  snprintf(token, sizeof token, "%s/token", root);
  snprintf(link, sizeof link, "%s/link", root);
  snprintf(fifo, sizeof fifo, "%s/fifo", root);
  struct deploy_token_watch watch = {0};
  CHECK(!deploy_token_file_active(token, &watch, 90, 1));
  write_text(token, "legacy token");
  CHECK(deploy_token_file_active(token, &watch, 90, 1));
  CHECK(symlink(token, link) == 0);
  watch = (struct deploy_token_watch){0};
  CHECK(!deploy_token_file_active(link, &watch, 90, 1));
  CHECK(deploy_token_file_active(link, &watch, 90, 0));
  CHECK(mkfifo(fifo, 0600) == 0);
  CHECK(!deploy_token_file_active(fifo, &watch, 90, 1));

  const struct timespec epoch[2] = {{0, 0}, {0, 0}};
  CHECK(utimensat(AT_FDCWD, token, epoch, 0) == 0);
  watch = (struct deploy_token_watch){0};
  CHECK(!deploy_token_file_active(token, &watch, 90, 1));
  snprintf(text, sizeof text, "deploy %.3f\n", tc002_clock_seconds(CLOCK_BOOTTIME));
  write_text(token, text);
  CHECK(utimensat(AT_FDCWD, token, epoch, 0) == 0);
  watch = (struct deploy_token_watch){0};
  CHECK(deploy_token_file_active(token, &watch, 90, 1));
  CHECK(tc002_sync_parent(token) == 0);
  CHECK(!tc002_mounted(root));
  CHECK(tc002_clock_seconds((clockid_t)-999) < 0);

  unlink(link);
  unlink(fifo);
  unlink(token);
  rmdir(root);
}

int main(void) {
  test_deploy_token_files();
  test_decisions();
  test_deploy_hold();
  test_vendor_only_on_request();
  test_parse_attempts();
  test_upgrade_inputs();
  test_state_files();
  if (awtrix_test_failures) {
    fprintf(stderr, "%d check(s) failed\n", awtrix_test_failures);
    return 1;
  }
  puts("loader policy tests passed");
  return 0;
}
