#include "test_support.h"

#include <stdlib.h>
#include <string.h>

static int bb_test_should_fail(bb_test_allocator_state *state) {
  state->attempt_count += 1;
  return state->fail_at_attempt != 0 && state->attempt_count == state->fail_at_attempt;
}

static void *bb_test_alloc(void *user, size_t size, size_t alignment) {
  bb_test_allocator_state *state = user;
  void *pointer;
  state->last_size = size;
  state->last_alignment = alignment;
  if (bb_test_should_fail(state)) {
    return NULL;
  }
  pointer = malloc(size);
  if (pointer != NULL) {
    state->allocation_count += 1;
    state->outstanding_allocations += 1;
    state->outstanding_bytes += size;
  }
  return pointer;
}

static void *bb_test_realloc(void *user, void *pointer, size_t old_size, size_t new_size, size_t alignment) {
  bb_test_allocator_state *state = user;
  const int was_null = pointer == NULL;
  void *new_pointer;
  state->last_size = new_size;
  state->last_alignment = alignment;
  if (bb_test_should_fail(state)) {
    return NULL;
  }
  new_pointer = realloc(pointer, new_size);
  if (new_pointer != NULL) {
    state->allocation_count += 1;
    if (was_null) {
      state->outstanding_allocations += 1;
    }
    state->outstanding_bytes = state->outstanding_bytes - old_size + new_size;
  }
  return new_pointer;
}

static void bb_test_free(void *user, void *pointer, size_t size, size_t alignment) {
  bb_test_allocator_state *state = user;
  (void)alignment;
  if (pointer == NULL) {
    return;
  }
  state->free_count += 1;
  state->outstanding_allocations -= 1;
  state->outstanding_bytes -= size;
  free(pointer);
}

bb_context_desc bb_test_context_desc(bb_test_allocator_state *state) {
  bb_context_desc desc;
  bb_context_desc_init(&desc);
  desc.allocator.user = state;
  desc.allocator.alloc = bb_test_alloc;
  desc.allocator.realloc = bb_test_realloc;
  desc.allocator.free = bb_test_free;
  return desc;
}

int bb_test_string_equal(bb_string_view value, const char *expected) {
  const size_t expected_length = strlen(expected);
  return value.length == expected_length && (expected_length == 0 || memcmp(value.data, expected, expected_length) == 0);
}

int bb_run_test_suite(const bb_test_case *tests, size_t test_count, size_t *io_index, size_t *io_passed) {
  size_t local_index;
  for (local_index = 0; local_index < test_count; local_index += 1) {
    *io_index += 1;
    if (!tests[local_index].function()) {
      fprintf(stderr, "not ok %lu - %s\n", (unsigned long)*io_index, tests[local_index].name);
      continue;
    }
    printf("ok %lu - %s\n", (unsigned long)*io_index, tests[local_index].name);
    *io_passed += 1;
  }
  return 1;
}
