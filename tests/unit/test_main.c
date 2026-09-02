#include "test_support.h"

#include <stdlib.h>

int main(void) {
  size_t index = 0;
  size_t passed = 0;
  size_t total = bb_context_test_count + bb_foundation_test_count + bb_collection_test_count + bb_syntax_test_count +
                 bb_program_test_count + bb_wasm_test_count + bb_wii_test_count;
#if defined(BB_HAS_RASTER)
  total += bb_graph_test_count + bb_raster_test_count;
#endif
  bb_run_test_suite(bb_context_tests, bb_context_test_count, &index, &passed);
  bb_run_test_suite(bb_foundation_tests, bb_foundation_test_count, &index, &passed);
  bb_run_test_suite(bb_collection_tests, bb_collection_test_count, &index, &passed);
  bb_run_test_suite(bb_wii_tests, bb_wii_test_count, &index, &passed);
  bb_run_test_suite(bb_syntax_tests, bb_syntax_test_count, &index, &passed);
  bb_run_test_suite(bb_program_tests, bb_program_test_count, &index, &passed);
  bb_run_test_suite(bb_wasm_tests, bb_wasm_test_count, &index, &passed);
#if defined(BB_HAS_RASTER)
  bb_run_test_suite(bb_graph_tests, bb_graph_test_count, &index, &passed);
  bb_run_test_suite(bb_raster_tests, bb_raster_test_count, &index, &passed);
#endif
  printf("%lu/%lu tests passed\n", (unsigned long)passed, (unsigned long)total);
  return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
