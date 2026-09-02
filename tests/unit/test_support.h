#ifndef BINBLOCK_TEST_SUPPORT_H
#define BINBLOCK_TEST_SUPPORT_H

#include <binblock/binblock.h>

#include <stddef.h>
#include <stdio.h>

typedef int (*bb_test_fn)(void);

typedef struct bb_test_case {
  const char *name;
  bb_test_fn function;
} bb_test_case;

typedef struct bb_test_allocator_state {
  size_t attempt_count;
  size_t allocation_count;
  size_t free_count;
  size_t outstanding_allocations;
  size_t outstanding_bytes;
  size_t last_size;
  size_t last_alignment;
  size_t fail_at_attempt;
} bb_test_allocator_state;

#define BB_TEST_ASSERT(expression)                                                                                           \
  do {                                                                                                                       \
    if (!(expression)) {                                                                                                    \
      fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #expression);                                  \
      return 0;                                                                                                              \
    }                                                                                                                        \
  } while (0)

#define BB_TEST_STRING(literal) ((bb_string_view){(literal), sizeof(literal) - 1})
#define BB_TEST_BYTES(literal) ((bb_bytes){(const uint8_t *)(literal), sizeof(literal) - 1})

bb_context_desc bb_test_context_desc(bb_test_allocator_state *state);
int bb_test_string_equal(bb_string_view value, const char *expected);
int bb_run_test_suite(const bb_test_case *tests, size_t test_count, size_t *io_index, size_t *io_passed);

extern const bb_test_case bb_context_tests[];
extern const size_t bb_context_test_count;
extern const bb_test_case bb_foundation_tests[];
extern const size_t bb_foundation_test_count;
extern const bb_test_case bb_collection_tests[];
extern const size_t bb_collection_test_count;
extern const bb_test_case bb_wii_tests[];
extern const size_t bb_wii_test_count;
extern const bb_test_case bb_syntax_tests[];
extern const size_t bb_syntax_test_count;
extern const bb_test_case bb_program_tests[];
extern const size_t bb_program_test_count;
extern const bb_test_case bb_wasm_tests[];
extern const size_t bb_wasm_test_count;
#if defined(BB_HAS_RASTER)
extern const bb_test_case bb_graph_tests[];
extern const size_t bb_graph_test_count;
extern const bb_test_case bb_raster_tests[];
extern const size_t bb_raster_test_count;
#endif

#endif
