#include <unity.h>

#include "core/api/SingleUploadRequest.h"

using namespace awtrix::api;

void setUp() {}
void tearDown() {}

namespace {
HttpResult policy(int status) { return HttpResult{status, "application/json", "policy"}; }

void test_aborted_authenticated_update_cannot_authorize_later_empty_post() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  TEST_ASSERT_TRUE(request.accepting());
  request.abort();
  TEST_ASSERT_FALSE(request.accepting());
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  request.reset([&] { ++cancelled; });
  const auto result = request.complete(policy(401));
  TEST_ASSERT_EQUAL_INT(401, result.status);
  TEST_ASSERT_FALSE(request.started());
  TEST_ASSERT_FALSE(request.ended());
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
}

void test_parser_failure_without_abort_callback_is_cancelled_at_cleanup() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.abort(); // The production server's post-handleClient cleanup.
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  request.abort();
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
}

void test_new_request_selection_cancels_abandoned_update_and_clears_state() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  TEST_ASSERT_FALSE(request.accepting());
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
}

void test_update_requires_start_and_end_before_success() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  TEST_ASSERT_FALSE(request.accepting());
  TEST_ASSERT_EQUAL_INT(200, request.complete(policy(200)).status);
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
}

void test_update_completion_rechecks_current_authorization() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  TEST_ASSERT_EQUAL_INT(401, request.complete(policy(401)).status);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
}

void test_start_policy_failures_never_open_resources() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  for (int status : {400, 401, 403, 405}) {
    request.reset([&] { ++cancelled; });
    TEST_ASSERT_FALSE(request.start(policy(status)));
    TEST_ASSERT_FALSE(request.accepting());
    request.end();
    TEST_ASSERT_EQUAL_INT(status, request.complete(policy(200)).status);
  }
  TEST_ASSERT_EQUAL_UINT(0, cancelled);
}

void test_second_file_cannot_replace_success_or_hide_failure() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  TEST_ASSERT_FALSE(request.start(policy(200)));
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.fail(HttpResult{500, "application/json", "flash failure"});
  TEST_ASSERT_FALSE(request.start(policy(200)));
  TEST_ASSERT_EQUAL_INT(500, request.complete(policy(200)).status);
  TEST_ASSERT_EQUAL_UINT(2, cancelled);
}

void test_bad_image_error_is_retained_and_cancels_update_once() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.fail(HttpResult{400, "application/json", "wrong chip"});
  request.abort();
  request.end();
  const auto result = request.complete(policy(200));
  TEST_ASSERT_EQUAL_INT(400, result.status);
  TEST_ASSERT_EQUAL_STRING("wrong chip", result.body.c_str());
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
}

void test_completed_part_followed_by_disconnected_multipart_never_succeeds() {
  SingleUploadRequest request;
  unsigned cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  request.abort();
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200)).status);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
}

void test_restore_keeps_its_own_authorization_when_backup_changes_credentials() {
  SingleUploadRequest request;
  request.reset();
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  TEST_ASSERT_EQUAL_INT(200, request.complete(policy(401),
      SingleUploadRequest::CompletionAuthorization::Start).status);
  request.reset();
  TEST_ASSERT_EQUAL_INT(401, request.complete(policy(401),
      SingleUploadRequest::CompletionAuthorization::Start).status);
}

void test_restore_cannot_reuse_rejected_or_unselected_authorization() {
  SingleUploadRequest request;
  TEST_ASSERT_FALSE(request.start(policy(200)));
  TEST_ASSERT_EQUAL_INT(401, request.complete(policy(401),
      SingleUploadRequest::CompletionAuthorization::Start).status);
  request.reset();
  TEST_ASSERT_FALSE(request.start(policy(401)));
  TEST_ASSERT_EQUAL_INT(401, request.complete(policy(200),
      SingleUploadRequest::CompletionAuthorization::Start).status);
}

void test_restore_start_authorization_does_not_hide_incomplete_or_multiple_files() {
  SingleUploadRequest request;
  request.reset();
  TEST_ASSERT_TRUE(request.start(policy(200)));
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(401),
      SingleUploadRequest::CompletionAuthorization::Start).status);
  request.reset();
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  TEST_ASSERT_FALSE(request.start(policy(401)));
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(401),
      SingleUploadRequest::CompletionAuthorization::Start).status);
}

void test_activation_runs_once_only_after_successful_request_completion() {
  SingleUploadRequest request;
  unsigned cancelled = 0, activated = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  TEST_ASSERT_EQUAL_UINT(0, activated);
  const auto activate = [&] {
    TEST_ASSERT_EQUAL_UINT(0, cancelled);
    ++activated;
    return HttpResult{};
  };
  TEST_ASSERT_EQUAL_INT(200, request.complete(policy(200),
      SingleUploadRequest::CompletionAuthorization::Current, activate).status);
  TEST_ASSERT_EQUAL_UINT(1, activated);
  TEST_ASSERT_EQUAL_UINT(0, cancelled);
  TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200),
      SingleUploadRequest::CompletionAuthorization::Current, activate).status);
  TEST_ASSERT_EQUAL_UINT(1, activated);
}

void test_duplicate_part_disconnect_and_failed_done_auth_never_activate() {
  for (int rejection = 0; rejection < 3; ++rejection) {
    SingleUploadRequest request;
    unsigned cancelled = 0, activated = 0;
    request.reset([&] { ++cancelled; });
    TEST_ASSERT_TRUE(request.start(policy(200)));
    request.end();
    if (rejection == 0) TEST_ASSERT_FALSE(request.start(policy(200)));
    if (rejection == 1) request.abort();
    const auto result = request.complete(policy(rejection == 2 ? 401 : 200),
        SingleUploadRequest::CompletionAuthorization::Current, [&] {
      ++activated;
      return HttpResult{};
    });
    TEST_ASSERT_EQUAL_INT(rejection == 2 ? 401 : 400, result.status);
    TEST_ASSERT_EQUAL_UINT(0, activated);
    TEST_ASSERT_EQUAL_UINT(1, cancelled);
  }
}

void test_missing_start_or_end_never_invokes_finalizer() {
  for (bool started : {false, true}) {
    SingleUploadRequest request;
    unsigned finalized = 0;
    request.reset();
    if (started) TEST_ASSERT_TRUE(request.start(policy(200)));
    TEST_ASSERT_EQUAL_INT(400, request.complete(policy(200),
        SingleUploadRequest::CompletionAuthorization::Current, [&] {
      ++finalized;
      return HttpResult{};
    }).status);
    TEST_ASSERT_EQUAL_UINT(0, finalized);
  }
}

void test_finalization_failure_cancels_live_update_and_cannot_be_retried() {
  SingleUploadRequest request;
  unsigned finalized = 0, cancelled = 0;
  request.reset([&] { ++cancelled; });
  TEST_ASSERT_TRUE(request.start(policy(200)));
  request.end();
  const auto failFinalization = [&] {
    ++finalized;
    TEST_ASSERT_EQUAL_UINT(0, cancelled);
    return HttpResult{500, "application/json", "activation failed"};
  };
  const auto result = request.complete(policy(200),
      SingleUploadRequest::CompletionAuthorization::Current, failFinalization);
  TEST_ASSERT_EQUAL_INT(500, result.status);
  TEST_ASSERT_EQUAL_STRING("activation failed", result.body.c_str());
  TEST_ASSERT_EQUAL_UINT(1, finalized);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
  TEST_ASSERT_EQUAL_INT(500, request.complete(policy(200),
      SingleUploadRequest::CompletionAuthorization::Current, failFinalization).status);
  TEST_ASSERT_EQUAL_UINT(1, finalized);
  TEST_ASSERT_EQUAL_UINT(1, cancelled);
}
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_aborted_authenticated_update_cannot_authorize_later_empty_post);
  RUN_TEST(test_parser_failure_without_abort_callback_is_cancelled_at_cleanup);
  RUN_TEST(test_new_request_selection_cancels_abandoned_update_and_clears_state);
  RUN_TEST(test_update_requires_start_and_end_before_success);
  RUN_TEST(test_update_completion_rechecks_current_authorization);
  RUN_TEST(test_start_policy_failures_never_open_resources);
  RUN_TEST(test_second_file_cannot_replace_success_or_hide_failure);
  RUN_TEST(test_bad_image_error_is_retained_and_cancels_update_once);
  RUN_TEST(test_completed_part_followed_by_disconnected_multipart_never_succeeds);
  RUN_TEST(test_restore_keeps_its_own_authorization_when_backup_changes_credentials);
  RUN_TEST(test_restore_cannot_reuse_rejected_or_unselected_authorization);
  RUN_TEST(test_restore_start_authorization_does_not_hide_incomplete_or_multiple_files);
  RUN_TEST(test_activation_runs_once_only_after_successful_request_completion);
  RUN_TEST(test_duplicate_part_disconnect_and_failed_done_auth_never_activate);
  RUN_TEST(test_missing_start_or_end_never_invokes_finalizer);
  RUN_TEST(test_finalization_failure_cancels_live_update_and_cannot_be_retried);
  return UNITY_END();
}
