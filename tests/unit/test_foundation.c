#include "test_support.h"

#include "context_internal.h"
#include "diagnostic.h"

#include <stdint.h>
#include <string.h>

static int test_diagnostics_are_owned_ordered_data(void) {
  char first_message[] = "first";
  const bb_span related[] = {{1, 3, 4}, {1, 5, 6}};
  bb_context *context = NULL;
  bb_source_id source_id;
  bb_diagnostic_store store;
  bb_diagnostic diagnostic;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_add_source(context, BB_TEST_STRING("main"), BB_TEST_BYTES("abcdef"), &source_id) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_diagnostic_store_init(&store, context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_diagnostic_store_push(
      &store,
      BB_DIAGNOSTIC_ERROR,
      200,
      (bb_string_view){first_message, 5},
      (bb_span){source_id, 1, 2},
      related,
      2
    ) == BB_STATUS_OK
  );
  first_message[0] = 'X';
  BB_TEST_ASSERT(
    bb_diagnostic_store_push(
      &store,
      BB_DIAGNOSTIC_NOTE,
      100,
      BB_TEST_STRING("second"),
      (bb_span){source_id, 0, 0},
      NULL,
      0
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_diagnostic_store_count(&store) == 2);
  BB_TEST_ASSERT(bb_diagnostic_store_get(&store, 0, &diagnostic) == BB_STATUS_OK);
  BB_TEST_ASSERT(diagnostic.code == 200 && diagnostic.severity == BB_DIAGNOSTIC_ERROR);
  BB_TEST_ASSERT(bb_test_string_equal(diagnostic.message, "first"));
  BB_TEST_ASSERT(diagnostic.related_span_count == 2);
  BB_TEST_ASSERT(diagnostic.related_spans[1].byte_start == 5);
  BB_TEST_ASSERT(bb_diagnostic_store_get(&store, 1, &diagnostic) == BB_STATUS_OK);
  BB_TEST_ASSERT(diagnostic.code == 100 && bb_test_string_equal(diagnostic.message, "second"));
  BB_TEST_ASSERT(bb_diagnostic_store_get(&store, 2, &diagnostic) == BB_STATUS_NOT_FOUND);
  bb_diagnostic_store_destroy(&store);
  bb_context_destroy(context);
  return 1;
}

static int test_diagnostic_limits_ranges_utf8_and_failures(void) {
  static const char invalid[] = {(char)0xff};
  size_t failure_attempt;
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_diagnostic_store store;
  bb_context_desc_init(&desc);
  desc.limits.max_diagnostics = 1;
  desc.limits.max_related_spans = 0;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_diagnostic_store_init(&store, context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_diagnostic_store_push(&store, BB_DIAGNOSTIC_ERROR, 1, (bb_string_view){invalid, 1}, (bb_span){0, 0, 0}, NULL, 0) ==
    BB_STATUS_INVALID_UTF8
  );
  BB_TEST_ASSERT(
    bb_diagnostic_store_push(&store, BB_DIAGNOSTIC_ERROR, 1, BB_TEST_STRING("error"), (bb_span){2, 0, 0}, NULL, 0) ==
    BB_STATUS_NOT_FOUND
  );
  BB_TEST_ASSERT(
    bb_diagnostic_store_push(&store, BB_DIAGNOSTIC_ERROR, 1, BB_TEST_STRING("error"), (bb_span){0, 0, 0}, NULL, 0) ==
    BB_STATUS_OK
  );
  BB_TEST_ASSERT(
    bb_diagnostic_store_push(&store, BB_DIAGNOSTIC_ERROR, 2, BB_TEST_STRING("later"), (bb_span){0, 0, 0}, NULL, 0) ==
    BB_STATUS_LIMIT_EXCEEDED
  );
  bb_diagnostic_store_destroy(&store);
  bb_context_destroy(context);

  for (failure_attempt = 2; failure_attempt <= 3; failure_attempt += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc failure_desc = bb_test_context_desc(&state);
    state.fail_at_attempt = failure_attempt;
    BB_TEST_ASSERT(bb_context_create(&failure_desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_diagnostic_store_init(&store, context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_diagnostic_store_push(&store, BB_DIAGNOSTIC_ERROR, 1, BB_TEST_STRING("error"), (bb_span){0, 0, 0}, NULL, 0) ==
      BB_STATUS_OUT_OF_MEMORY
    );
    BB_TEST_ASSERT(bb_diagnostic_store_count(&store) == 0);
    bb_diagnostic_store_destroy(&store);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

const bb_test_case bb_foundation_tests[] = {
  {"owned ordered diagnostics", test_diagnostics_are_owned_ordered_data},
  {"diagnostic validation and failures", test_diagnostic_limits_ranges_utf8_and_failures},
};

const size_t bb_foundation_test_count = sizeof(bb_foundation_tests) / sizeof(bb_foundation_tests[0]);
