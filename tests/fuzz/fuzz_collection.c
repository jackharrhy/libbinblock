#include <binblock/collection.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bb_value bb_fuzz_integer(uint8_t value) {
  bb_value result;
  memset(&result, 0, sizeof(result));
  result.kind = BB_VALUE_INTEGER;
  result.data.integer = value;
  return result;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_collection *left = NULL;
  bb_collection *right = NULL;
  bb_collection *product = NULL;
  bb_collection *slice = NULL;
  bb_collection *concatenated = NULL;
  bb_value left_values[32];
  bb_value right_values[32];
  bb_value output[2];
  const size_t left_count = size > 32 ? 32 : size;
  const size_t remaining = size > left_count ? size - left_count : 0;
  const size_t right_count = remaining > 32 ? 32 : remaining;
  uint64_t product_count = 0;
  uint64_t slice_count = 0;
  uint64_t start = 0;
  uint64_t wanted = 0;
  size_t index;

  for (index = 0; index < left_count; index += 1) left_values[index] = bb_fuzz_integer(data[index]);
  for (index = 0; index < right_count; index += 1)
    right_values[index] = bb_fuzz_integer(data[left_count + index]);
  bb_context_desc_init(&desc);
  desc.limits.max_collection_cardinality = 4096;
  if (bb_context_create(&desc, &context) != BB_STATUS_OK) return 0;
  if (bb_collection_from_values(context, left_values, left_count, &left) != BB_STATUS_OK ||
      bb_collection_from_values(context, right_values, right_count, &right) != BB_STATUS_OK ||
      bb_collection_product(left, right, &product) != BB_STATUS_OK ||
      bb_collection_count(product, &product_count) != BB_STATUS_OK)
    goto cleanup;
  if (product_count != (uint64_t)left_count * right_count) abort();
  if (size != 0) {
    start = data[0] % (product_count + 1);
    wanted = data[size - 1];
  }
  if (bb_collection_slice(product, start, wanted, &slice) == BB_STATUS_OK &&
      bb_collection_count(slice, &slice_count) == BB_STATUS_OK) {
    const uint64_t expected = start >= product_count
                                ? 0
                                : wanted < product_count - start ? wanted : product_count - start;
    if (slice_count != expected) abort();
    if (slice_count != 0 && bb_collection_get(slice, slice_count - 1, output, 2) != BB_STATUS_OK)
      abort();
  }
  if (bb_collection_concat(left, right, &concatenated) == BB_STATUS_OK) {
    uint64_t concatenated_count = 0;
    if (bb_collection_count(concatenated, &concatenated_count) != BB_STATUS_OK ||
        concatenated_count != left_count + right_count)
      abort();
  }

cleanup:
  bb_collection_release(concatenated);
  bb_collection_release(slice);
  bb_collection_release(product);
  bb_collection_release(right);
  bb_collection_release(left);
  bb_context_destroy(context);
  return 0;
}
