#include "test_support.h"

#include "checked_math.h"
#include "context_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct bb_test_log_state {
  size_t count;
  bb_log_level last_level;
  bb_string_view last_message;
} bb_test_log_state;

static void bb_test_log(void *user, bb_log_level level, bb_string_view message) {
  bb_test_log_state *state = user;
  state->count += 1;
  state->last_level = level;
  state->last_message = message;
}

static int test_default_context_lifetime(void) {
  bb_context *context = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(context != NULL);
  bb_context_destroy(context);
  bb_context_destroy(NULL);
  return 1;
}

static int test_custom_allocator_lifetime(void) {
  bb_test_allocator_state state = {0};
  bb_context_desc desc = bb_test_context_desc(&state);
  bb_context *context = NULL;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(context != NULL);
  BB_TEST_ASSERT(state.attempt_count == 1);
  BB_TEST_ASSERT(state.outstanding_allocations == 1);
  BB_TEST_ASSERT(state.last_size > 0);
  BB_TEST_ASSERT(state.last_alignment > 0);
  bb_context_destroy(context);
  BB_TEST_ASSERT(state.outstanding_allocations == 0);
  BB_TEST_ASSERT(state.outstanding_bytes == 0);
  return 1;
}

static int test_optional_context_logging_callback(void) {
  bb_test_log_state state = {0};
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_context_desc_init(&desc);
  desc.log_user = &state;
  desc.log = bb_test_log;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(state.count == 1);
  BB_TEST_ASSERT(state.last_level == BB_LOG_TRACE);
  BB_TEST_ASSERT(bb_test_string_equal(state.last_message, "context created"));
  bb_context_destroy(context);
  BB_TEST_ASSERT(state.count == 2);
  BB_TEST_ASSERT(state.last_level == BB_LOG_TRACE);
  BB_TEST_ASSERT(bb_test_string_equal(state.last_message, "context destroyed"));
  return 1;
}

static int test_context_allocation_failure(void) {
  bb_test_allocator_state state = {0};
  bb_context_desc desc = bb_test_context_desc(&state);
  bb_context *context = (bb_context *)(uintptr_t)1;
  state.fail_at_attempt = 1;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OUT_OF_MEMORY);
  BB_TEST_ASSERT(context == NULL);
  BB_TEST_ASSERT(state.attempt_count == 1);
  BB_TEST_ASSERT(state.outstanding_allocations == 0);
  return 1;
}

static int test_context_rejects_invalid_descriptors(void) {
  bb_test_allocator_state state = {0};
  bb_context_desc desc;
  bb_context *context = (bb_context *)(uintptr_t)1;
  BB_TEST_ASSERT(bb_context_create(NULL, NULL) == BB_STATUS_INVALID_ARGUMENT);

  bb_context_desc_init(&desc);
  desc.struct_size = 0;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(context == NULL);

  desc = bb_test_context_desc(&state);
  desc.allocator.realloc = NULL;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(context == NULL);

  bb_context_desc_init(&desc);
  desc.utf8_policy = (bb_utf8_policy)99;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(context == NULL);
  return 1;
}

static int test_default_limits_are_explicit(void) {
  bb_context_desc desc;
  bb_limits limits;
  bb_context *context = NULL;
  bb_context_desc_init(&desc);
  BB_TEST_ASSERT(desc.limits.max_sources > 0);
  BB_TEST_ASSERT(desc.limits.max_source_bytes > 0);
  BB_TEST_ASSERT(desc.limits.max_collection_cardinality > 0);
  BB_TEST_ASSERT(desc.limits.max_render_pixels > 0);
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_get_limits(context, &limits) == BB_STATUS_OK);
  BB_TEST_ASSERT(memcmp(&limits, &desc.limits, sizeof(limits)) == 0);
  BB_TEST_ASSERT(bb_context_get_limits(NULL, &limits) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_context_get_limits(context, NULL) == BB_STATUS_INVALID_ARGUMENT);
  bb_context_destroy(context);
  return 1;
}

static int test_checked_size_arithmetic(void) {
  size_t result = 0;
  BB_TEST_ASSERT(bb_size_add(20, 22, &result));
  BB_TEST_ASSERT(result == 42);
  BB_TEST_ASSERT(bb_size_multiply(6, 7, &result));
  BB_TEST_ASSERT(result == 42);
  BB_TEST_ASSERT(bb_size_multiply(SIZE_MAX, 0, &result));
  BB_TEST_ASSERT(result == 0);
  BB_TEST_ASSERT(!bb_size_add(SIZE_MAX, 1, &result));
  BB_TEST_ASSERT(!bb_size_multiply(SIZE_MAX, 2, &result));
  BB_TEST_ASSERT(!bb_size_add(1, 2, NULL));
  BB_TEST_ASSERT(!bb_size_multiply(1, 2, NULL));
  return 1;
}

static int test_utf8_validation(void) {
  static const uint8_t valid[] = {'A', 0xc2, 0xa2, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80};
  static const uint8_t lone_continuation[] = {0x80};
  static const uint8_t overlong_two[] = {0xc0, 0x80};
  static const uint8_t overlong_three[] = {0xe0, 0x80, 0x80};
  static const uint8_t surrogate[] = {0xed, 0xa0, 0x80};
  static const uint8_t above_unicode[] = {0xf4, 0x90, 0x80, 0x80};
  static const uint8_t truncated[] = {0xf0, 0x9f, 0x98};
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){NULL, 0}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){NULL, 1}) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){valid, sizeof(valid)}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){lone_continuation, sizeof(lone_continuation)}) == BB_STATUS_INVALID_UTF8);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){overlong_two, sizeof(overlong_two)}) == BB_STATUS_INVALID_UTF8);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){overlong_three, sizeof(overlong_three)}) == BB_STATUS_INVALID_UTF8);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){surrogate, sizeof(surrogate)}) == BB_STATUS_INVALID_UTF8);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){above_unicode, sizeof(above_unicode)}) == BB_STATUS_INVALID_UTF8);
  BB_TEST_ASSERT(bb_utf8_validate((bb_bytes){truncated, sizeof(truncated)}) == BB_STATUS_INVALID_UTF8);
  return 1;
}

static int test_sources_are_owned_and_spans_are_checked(void) {
  char name[] = "main.bb";
  uint8_t source[] = "size := 64";
  bb_source_info info;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_context *context = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      (bb_string_view){name, sizeof(name) - 1},
      (bb_bytes){source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  name[0] = 'X';
  source[0] = 'X';
  BB_TEST_ASSERT(source_id == 1);
  BB_TEST_ASSERT(bb_context_source_count(context) == 1);
  BB_TEST_ASSERT(bb_context_source_info(context, source_id, &info) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(info.name, "main.bb"));
  BB_TEST_ASSERT(info.contents.length == sizeof(source) - 1);
  BB_TEST_ASSERT(info.contents.data[0] == 's');
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){source_id, 0, 10}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){source_id, 10, 10}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){source_id, 10, 9}) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){source_id, 0, 11}) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){2, 0, 0}) == BB_STATUS_NOT_FOUND);
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){0, 0, 0}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_validate_span(context, (bb_span){0, 0, 1}) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_context_source_info(context, 0, &info) == BB_STATUS_NOT_FOUND);
  BB_TEST_ASSERT(bb_context_source_info(NULL, 1, &info) == BB_STATUS_INVALID_ARGUMENT);
  bb_context_destroy(context);
  return 1;
}

static int test_zero_length_source(void) {
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_source_info info;
  bb_context *context = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(context, (bb_string_view){NULL, 0}, (bb_bytes){NULL, 0}, &source_id) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_context_source_info(context, source_id, &info) == BB_STATUS_OK);
  BB_TEST_ASSERT(info.name.data == NULL && info.name.length == 0);
  BB_TEST_ASSERT(info.contents.data == NULL && info.contents.length == 0);
  bb_context_destroy(context);
  return 1;
}

static int test_source_utf8_policy(void) {
  static const uint8_t invalid[] = {0xff};
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_context_desc_init(&desc);
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(context, BB_TEST_STRING("invalid"), (bb_bytes){invalid, sizeof(invalid)}, &source_id) ==
    BB_STATUS_INVALID_UTF8
  );
  BB_TEST_ASSERT(source_id == BB_SOURCE_ID_NONE);
  bb_context_destroy(context);

  desc.utf8_policy = BB_UTF8_ALLOW_INVALID;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(context, BB_TEST_STRING("invalid"), (bb_bytes){invalid, sizeof(invalid)}, &source_id) ==
    BB_STATUS_OK
  );
  bb_context_destroy(context);
  return 1;
}

static int test_source_limits(void) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_context_desc_init(&desc);
  desc.limits.max_sources = 1;
  desc.limits.max_source_bytes = 3;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_add_source(context, BB_TEST_STRING("a"), BB_TEST_BYTES("1234"), &source_id) == BB_STATUS_LIMIT_EXCEEDED);
  BB_TEST_ASSERT(bb_context_add_source(context, BB_TEST_STRING("a"), BB_TEST_BYTES("123"), &source_id) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_add_source(context, BB_TEST_STRING("b"), BB_TEST_BYTES(""), &source_id) == BB_STATUS_LIMIT_EXCEEDED);
  bb_context_destroy(context);

  bb_context_desc_init(&desc);
  desc.limits.max_total_allocation_bytes = 1;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_add_source(context, BB_TEST_STRING("a"), BB_TEST_BYTES("b"), &source_id) == BB_STATUS_LIMIT_EXCEEDED);
  bb_context_destroy(context);
  return 1;
}

static int test_source_allocation_failures_are_clean(void) {
  size_t failure_attempt;
  for (failure_attempt = 2; failure_attempt <= 3; failure_attempt += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    state.fail_at_attempt = failure_attempt;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(context, BB_TEST_STRING("main"), BB_TEST_BYTES("code"), &source_id) == BB_STATUS_OUT_OF_MEMORY
    );
    BB_TEST_ASSERT(source_id == BB_SOURCE_ID_NONE);
    BB_TEST_ASSERT(bb_context_source_count(context) == 0);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
    BB_TEST_ASSERT(state.outstanding_bytes == 0);
  }
  return 1;
}

static int test_status_names_are_total(void) {
  BB_TEST_ASSERT(strcmp(bb_status_name(BB_STATUS_OK), "ok") == 0);
  BB_TEST_ASSERT(strcmp(bb_status_name(BB_STATUS_INVALID_UTF8), "invalid UTF-8") == 0);
  BB_TEST_ASSERT(strcmp(bb_status_name(BB_STATUS_CANCELLED), "cancelled") == 0);
  BB_TEST_ASSERT(strcmp(bb_status_name((bb_status)999), "unknown status") == 0);
  return 1;
}

const bb_test_case bb_context_tests[] = {
  {"default context lifetime", test_default_context_lifetime},
  {"custom allocator lifetime", test_custom_allocator_lifetime},
  {"optional context logging callback", test_optional_context_logging_callback},
  {"context allocation failure", test_context_allocation_failure},
  {"invalid context descriptors", test_context_rejects_invalid_descriptors},
  {"default limits", test_default_limits_are_explicit},
  {"checked size arithmetic", test_checked_size_arithmetic},
  {"UTF-8 validation", test_utf8_validation},
  {"owned sources and spans", test_sources_are_owned_and_spans_are_checked},
  {"zero-length source", test_zero_length_source},
  {"source UTF-8 policy", test_source_utf8_policy},
  {"source limits", test_source_limits},
  {"source allocation failures", test_source_allocation_failures_are_clean},
  {"status names", test_status_names_are_total},
};

const size_t bb_context_test_count = sizeof(bb_context_tests) / sizeof(bb_context_tests[0]);
