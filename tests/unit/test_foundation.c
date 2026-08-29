#include "test_support.h"

#include "arena.h"
#include "context_internal.h"
#include "diagnostic.h"
#include "interner.h"

#include <binblock/module.h>

#include <stdint.h>
#include <string.h>

static int test_arena_alignment_reset_and_limits(void) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_arena arena;
  void *first = NULL;
  void *second = NULL;
  bb_context_desc_init(&desc);
  desc.limits.max_arena_bytes = 16;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_arena_init(&arena, context, 16) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_arena_allocate(&arena, 1, 1, &first) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_arena_allocate(&arena, 4, 4, &second) == BB_STATUS_OK);
  BB_TEST_ASSERT(first != NULL && second != NULL);
  BB_TEST_ASSERT((uintptr_t)second % 4 == 0);
  BB_TEST_ASSERT(bb_arena_reserved_bytes(&arena) == 16);
  BB_TEST_ASSERT(bb_arena_allocate(&arena, 16, 1, &second) == BB_STATUS_LIMIT_EXCEEDED);
  BB_TEST_ASSERT(bb_arena_allocate(&arena, 1, 3, &second) == BB_STATUS_INVALID_ARGUMENT);
  BB_TEST_ASSERT(bb_arena_allocate(&arena, 0, 1, &second) == BB_STATUS_OK && second == NULL);
  BB_TEST_ASSERT(bb_context_allocation_bytes(context) > 0);
  bb_arena_reset(&arena);
  BB_TEST_ASSERT(bb_arena_reserved_bytes(&arena) == 0);
  BB_TEST_ASSERT(bb_context_allocation_bytes(context) == 0);
  bb_arena_destroy(&arena);
  bb_context_destroy(context);
  return 1;
}

static int test_arena_allocation_failure_is_clean(void) {
  bb_test_allocator_state state = {0};
  bb_context_desc desc = bb_test_context_desc(&state);
  bb_context *context = NULL;
  bb_arena arena;
  void *pointer = NULL;
  state.fail_at_attempt = 2;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_arena_init(&arena, context, 64) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_arena_allocate(&arena, 8, 8, &pointer) == BB_STATUS_OUT_OF_MEMORY);
  BB_TEST_ASSERT(pointer == NULL);
  BB_TEST_ASSERT(bb_arena_reserved_bytes(&arena) == 0);
  bb_arena_destroy(&arena);
  bb_context_destroy(context);
  BB_TEST_ASSERT(state.outstanding_allocations == 0);
  return 1;
}

static int test_interner_is_deterministic_and_owns_strings(void) {
  bb_context *left_context = NULL;
  bb_context *right_context = NULL;
  bb_interner left;
  bb_interner right;
  bb_symbol left_alpha;
  bb_symbol left_beta;
  bb_symbol duplicate_alpha;
  bb_symbol right_alpha;
  bb_symbol right_beta;
  bb_string_view value;
  char alpha[] = "alpha";
  BB_TEST_ASSERT(bb_context_create(NULL, &left_context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_create(NULL, &right_context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_init(&left, left_context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_init(&right, right_context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_intern(&left, (bb_string_view){alpha, 5}, &left_alpha) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_intern(&left, BB_TEST_STRING("beta"), &left_beta) == BB_STATUS_OK);
  alpha[0] = 'X';
  BB_TEST_ASSERT(bb_interner_intern(&left, BB_TEST_STRING("alpha"), &duplicate_alpha) == BB_STATUS_OK);
  BB_TEST_ASSERT(left_alpha == 1 && left_beta == 2 && duplicate_alpha == left_alpha);
  BB_TEST_ASSERT(bb_interner_count(&left) == 2);
  BB_TEST_ASSERT(bb_interner_lookup(&left, left_alpha, &value) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(value, "alpha"));
  BB_TEST_ASSERT(bb_interner_lookup(&left, 99, &value) == BB_STATUS_NOT_FOUND);

  BB_TEST_ASSERT(bb_interner_intern(&right, BB_TEST_STRING("alpha"), &right_alpha) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_intern(&right, BB_TEST_STRING("beta"), &right_beta) == BB_STATUS_OK);
  BB_TEST_ASSERT(right_alpha == left_alpha && right_beta == left_beta);
  bb_interner_destroy(&left);
  bb_interner_destroy(&right);
  BB_TEST_ASSERT(bb_context_allocation_bytes(left_context) == 0);
  BB_TEST_ASSERT(bb_context_allocation_bytes(right_context) == 0);
  bb_context_destroy(left_context);
  bb_context_destroy(right_context);
  return 1;
}

static int test_interner_limits_utf8_and_failures(void) {
  static const char invalid[] = {(char)0xff};
  size_t failure_attempt;
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_interner interner;
  bb_symbol symbol;
  bb_context_desc_init(&desc);
  desc.limits.max_interned_strings = 1;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_init(&interner, context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_intern(&interner, (bb_string_view){invalid, 1}, &symbol) == BB_STATUS_INVALID_UTF8);
  BB_TEST_ASSERT(bb_interner_intern(&interner, BB_TEST_STRING("one"), &symbol) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_interner_intern(&interner, BB_TEST_STRING("two"), &symbol) == BB_STATUS_LIMIT_EXCEEDED);
  bb_interner_destroy(&interner);
  bb_context_destroy(context);

  for (failure_attempt = 2; failure_attempt <= 3; failure_attempt += 1) {
    bb_test_allocator_state state = {0};
    bb_context_desc failure_desc = bb_test_context_desc(&state);
    state.fail_at_attempt = failure_attempt;
    BB_TEST_ASSERT(bb_context_create(&failure_desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_interner_init(&interner, context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_interner_intern(&interner, BB_TEST_STRING("value"), &symbol) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(symbol == BB_SYMBOL_NONE);
    BB_TEST_ASSERT(bb_interner_count(&interner) == 0);
    bb_interner_destroy(&interner);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

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

static int test_precompiled_module_uses_versioned_big_endian_envelope(void) {
  static const uint8_t source[] = "import \"binblock/basic\"\n";
  uint8_t encoded[128];
  size_t measured = 0;
  size_t written = 0;
  bb_precompiled_module_info info;
  bb_bytes decoded;
  BB_TEST_ASSERT(
    bb_precompiled_module_measure((bb_bytes){source, sizeof(source) - 1}, &measured) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(measured == BB_PRECOMPILED_MODULE_HEADER_SIZE + sizeof(source) - 1);
  BB_TEST_ASSERT(
    bb_precompiled_module_write(
      (bb_bytes){source, sizeof(source) - 1}, encoded, sizeof(encoded), &written
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(written == measured);
  BB_TEST_ASSERT(encoded[0] == 'B' && encoded[1] == 'B' && encoded[2] == 'M' && encoded[3] == 0);
  BB_TEST_ASSERT(encoded[4] == 0 && encoded[5] == BB_PRECOMPILED_MODULE_VERSION);
  BB_TEST_ASSERT(encoded[14] == 0 && encoded[15] == sizeof(source) - 1);
  BB_TEST_ASSERT(
    bb_precompiled_module_read((bb_bytes){encoded, written}, &info, &decoded) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(info.version == BB_PRECOMPILED_MODULE_VERSION && decoded.length == sizeof(source) - 1);
  BB_TEST_ASSERT(memcmp(decoded.data, source, decoded.length) == 0);
  encoded[written - 1] ^= 1;
  BB_TEST_ASSERT(
    bb_precompiled_module_read((bb_bytes){encoded, written}, &info, &decoded) == BB_STATUS_INVALID_ARGUMENT
  );
  return 1;
}

const bb_test_case bb_foundation_tests[] = {
  {"arena alignment, reset, and limits", test_arena_alignment_reset_and_limits},
  {"arena allocation failure", test_arena_allocation_failure_is_clean},
  {"deterministic string interning", test_interner_is_deterministic_and_owns_strings},
  {"interner limits and failures", test_interner_limits_utf8_and_failures},
  {"owned ordered diagnostics", test_diagnostics_are_owned_ordered_data},
  {"diagnostic validation and failures", test_diagnostic_limits_ranges_utf8_and_failures},
  {"precompiled module big-endian envelope", test_precompiled_module_uses_versioned_big_endian_envelope},
};

const size_t bb_foundation_test_count = sizeof(bb_foundation_tests) / sizeof(bb_foundation_tests[0]);
