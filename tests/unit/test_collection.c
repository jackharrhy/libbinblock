#include "test_support.h"

#include <binblock/collection.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bb_value bb_integer(int64_t value) {
  bb_value result;
  memset(&result, 0, sizeof(result));
  result.kind = BB_VALUE_INTEGER;
  result.data.integer = value;
  return result;
}

static bb_status map_double(
  void *user,
  const bb_value *input,
  size_t input_count,
  bb_value *output,
  size_t output_count
) {
  (void)user;
  if (input_count != 1 || output_count != 1 || input[0].kind != BB_VALUE_INTEGER)
    return BB_STATUS_INVALID_ARGUMENT;
  output[0] = bb_integer(input[0].data.integer * 2);
  return BB_STATUS_OK;
}

static bb_status map_invalid_number(
  void *user,
  const bb_value *input,
  size_t input_count,
  bb_value *output,
  size_t output_count
) {
  (void)user;
  (void)input;
  (void)input_count;
  if (output_count != 1) return BB_STATUS_INVALID_ARGUMENT;
  memset(output, 0, sizeof(*output));
  output[0].kind = BB_VALUE_NUMBER;
  output[0].data.number = NAN;
  return BB_STATUS_OK;
}

static bb_status predicate_even(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint32_t *out_matches
) {
  (void)user;
  if (input_count != 1 || input[0].kind != BB_VALUE_INTEGER || out_matches == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  *out_matches = (uint32_t)((input[0].data.integer & 1) == 0);
  return BB_STATUS_OK;
}

static bb_status flat_integer_count(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint64_t *out_count
) {
  (void)user;
  if (input_count != 1 || input[0].kind != BB_VALUE_INTEGER || input[0].data.integer < 0 || out_count == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  *out_count = (uint64_t)input[0].data.integer;
  return BB_STATUS_OK;
}

static bb_status flat_integer_get(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint64_t nested_index,
  bb_value *output,
  size_t output_count
) {
  (void)user;
  if (input_count != 1 || output_count != 1 || input[0].kind != BB_VALUE_INTEGER)
    return BB_STATUS_INVALID_ARGUMENT;
  output[0] = bb_integer((int64_t)nested_index);
  return BB_STATUS_OK;
}

static int test_collection_combinators_and_owned_values(void) {
  bb_context *context = NULL;
  bb_collection *base = NULL;
  bb_collection *other = NULL;
  bb_collection *shorter = NULL;
  bb_collection *mapped = NULL;
  bb_collection *invalid_map = NULL;
  bb_collection *filtered = NULL;
  bb_collection *flattened = NULL;
  bb_collection *concatenated = NULL;
  bb_collection *zipped = NULL;
  bb_collection *bad_zip = NULL;
  bb_collection *artifacts = NULL;
  bb_collection *selected = NULL;
  bb_value values[3] = {bb_integer(1), bb_integer(2), bb_integer(3)};
  bb_value other_values[3] = {bb_integer(4), bb_integer(5), bb_integer(6)};
  bb_value short_values[2] = {bb_integer(7), bb_integer(8)};
  bb_value artifact_values[3];
  bb_value output[2];
  uint64_t count;
  char first_key[] = "same";
  char middle_key[] = "other";
  char last_key[] = "same";
  char first_path[] = "a.png";
  char middle_path[] = "b.png";
  char last_path[] = "c.png";
  size_t index;

  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_from_values(context, values, 3, &base) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_from_values(context, other_values, 3, &other) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_from_values(context, short_values, 2, &shorter) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_map(base, 1, map_double, NULL, &mapped) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_get(mapped, 2, output, 1) == BB_STATUS_OK);
  BB_TEST_ASSERT(output[0].kind == BB_VALUE_INTEGER && output[0].data.integer == 6);
  BB_TEST_ASSERT(bb_collection_map(base, 1, map_invalid_number, NULL, &invalid_map) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_get(invalid_map, 0, output, 1) == BB_STATUS_INVALID_ARGUMENT);

  BB_TEST_ASSERT(bb_collection_filter(base, predicate_even, NULL, &filtered) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(filtered, &count) == BB_STATUS_OK && count == 1);
  BB_TEST_ASSERT(bb_collection_get(filtered, 0, output, 1) == BB_STATUS_OK && output[0].data.integer == 2);
  BB_TEST_ASSERT(bb_collection_flat_map(base, 1, flat_integer_count, flat_integer_get, NULL, &flattened) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(flattened, &count) == BB_STATUS_OK && count == 6);
  BB_TEST_ASSERT(bb_collection_get(flattened, 4, output, 1) == BB_STATUS_OK && output[0].data.integer == 1);

  BB_TEST_ASSERT(bb_collection_concat(base, other, &concatenated) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(concatenated, &count) == BB_STATUS_OK && count == 6);
  BB_TEST_ASSERT(bb_collection_get(concatenated, 3, output, 1) == BB_STATUS_OK && output[0].data.integer == 4);
  BB_TEST_ASSERT(bb_collection_zip(base, other, &zipped) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(zipped, &count) == BB_STATUS_OK && count == 3);
  BB_TEST_ASSERT(bb_collection_item_width(zipped) == 2);
  BB_TEST_ASSERT(bb_collection_get(zipped, 1, output, 2) == BB_STATUS_OK);
  BB_TEST_ASSERT(output[0].data.integer == 2 && output[1].data.integer == 5);
  BB_TEST_ASSERT(bb_collection_zip(base, shorter, &bad_zip) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(bad_zip, &count) == BB_STATUS_INVALID_ARGUMENT);

  memset(artifact_values, 0, sizeof(artifact_values));
  for (index = 0; index < 3; index += 1) {
    artifact_values[index].kind = BB_VALUE_ARTIFACT;
    artifact_values[index].data.artifact.alias_identity = BB_ALIAS_NONE;
  }
  artifact_values[0].data.artifact.key = (bb_string_view){first_key, sizeof(first_key) - 1};
  artifact_values[0].data.artifact.path = (bb_string_view){first_path, sizeof(first_path) - 1};
  artifact_values[1].data.artifact.key = (bb_string_view){middle_key, sizeof(middle_key) - 1};
  artifact_values[1].data.artifact.path = (bb_string_view){middle_path, sizeof(middle_path) - 1};
  artifact_values[2].data.artifact.key = (bb_string_view){last_key, sizeof(last_key) - 1};
  artifact_values[2].data.artifact.path = (bb_string_view){last_path, sizeof(last_path) - 1};
  BB_TEST_ASSERT(bb_collection_from_values(context, artifact_values, 3, &artifacts) == BB_STATUS_OK);
  first_key[0] = 'X';
  first_path[0] = 'X';
  BB_TEST_ASSERT(bb_collection_select_key(artifacts, BB_TEST_STRING("same"), &selected) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(selected, &count) == BB_STATUS_OK && count == 2);
  BB_TEST_ASSERT(bb_collection_get(selected, 0, output, 1) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(output[0].data.artifact.key, "same"));
  BB_TEST_ASSERT(bb_test_string_equal(output[0].data.artifact.path, "a.png"));
  BB_TEST_ASSERT(bb_collection_get(selected, 1, output, 1) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(output[0].data.artifact.path, "c.png"));
  BB_TEST_ASSERT(bb_collection_get(selected, 2, output, 1) == BB_STATUS_NOT_FOUND);

  bb_collection_release(selected);
  bb_collection_release(artifacts);
  bb_collection_release(bad_zip);
  bb_collection_release(zipped);
  bb_collection_release(concatenated);
  bb_collection_release(flattened);
  bb_collection_release(filtered);
  bb_collection_release(invalid_map);
  bb_collection_release(mapped);
  bb_collection_release(shorter);
  bb_collection_release(other);
  bb_collection_release(base);
  bb_context_destroy(context);
  return 1;
}

typedef struct artifact_map_state {
  bb_image_node images[19];
  char keys[304][16];
  char paths[304][32];
  size_t calls;
} artifact_map_state;

static bb_status map_product_artifact(
  void *user,
  const bb_value *input,
  size_t input_count,
  bb_value *output,
  size_t output_count
) {
  artifact_map_state *state = user;
  size_t mask_index;
  uint64_t artifact_index;
  if (input_count != 2 || output_count != 1 || input[0].kind != BB_VALUE_INTEGER ||
      input[1].kind != BB_VALUE_IMAGE) return BB_STATUS_INVALID_ARGUMENT;
  for (mask_index = 0; mask_index < 19; mask_index += 1) {
    if (state->images[mask_index] == input[1].data.image) break;
  }
  if (mask_index == 19 || input[0].data.integer < 0 || input[0].data.integer >= 16)
    return BB_STATUS_INVALID_ARGUMENT;
  artifact_index = (uint64_t)input[0].data.integer * 19 + mask_index;
  memset(output, 0, sizeof(*output));
  output[0].kind = BB_VALUE_ARTIFACT;
  output[0].data.artifact.key = (bb_string_view){state->keys[artifact_index], strlen(state->keys[artifact_index])};
  output[0].data.artifact.path = (bb_string_view){state->paths[artifact_index], strlen(state->paths[artifact_index])};
  output[0].data.artifact.image = input[1].data.image;
  output[0].data.artifact.alias_identity = BB_ALIAS_NONE;
  state->calls += 1;
  return BB_STATUS_OK;
}

typedef struct render_probe {
  size_t calls;
} render_probe;

static bb_status render_probe_raster(
  void *user,
  bb_context *context,
  const bb_image_graph *graph,
  const bb_artifact_value *artifact,
  bb_surface **out_surface
) {
  render_probe *probe = user;
  probe->calls += 1;
  return bb_image_graph_render_raster(context, graph, artifact->image, out_surface);
}

static int test_lazy_product_slice_and_render_gate(void) {
  bb_test_allocator_state allocator_state = {0};
  bb_context_desc desc = bb_test_context_desc(&allocator_state);
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_collection *colors = NULL;
  bb_collection *masks = NULL;
  bb_collection *product = NULL;
  bb_collection *artifacts = NULL;
  bb_collection *slice = NULL;
  bb_value color_values[16];
  bb_value mask_values[19];
  bb_value pair[2];
  bb_value artifact;
  artifact_map_state map_state;
  render_probe probe = {0};
  uint64_t count;
  size_t attempts_before_count;
  size_t index;
  memset(&map_state, 0, sizeof(map_state));
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  for (index = 0; index < 16; index += 1) color_values[index] = bb_integer((int64_t)index);
  for (index = 0; index < 19; index += 1) {
    BB_TEST_ASSERT(
      bb_image_graph_add_fill(graph, 1, 1, (bb_rgba8){(uint8_t)index, 0, 0, 255}, &map_state.images[index]) ==
      BB_STATUS_OK
    );
    memset(&mask_values[index], 0, sizeof(mask_values[index]));
    mask_values[index].kind = BB_VALUE_IMAGE;
    mask_values[index].data.image = map_state.images[index];
  }
  for (index = 0; index < 304; index += 1) {
    (void)snprintf(map_state.keys[index], sizeof(map_state.keys[index]), "c%02lu-m%02lu", (unsigned long)(index / 19),
                   (unsigned long)(index % 19));
    (void)snprintf(map_state.paths[index], sizeof(map_state.paths[index]), "preview/%s.png", map_state.keys[index]);
  }
  BB_TEST_ASSERT(bb_image_graph_seal(graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_from_values(context, color_values, 16, &colors) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_from_values(context, mask_values, 19, &masks) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_product(colors, masks, &product) == BB_STATUS_OK);
  attempts_before_count = allocator_state.attempt_count;
  BB_TEST_ASSERT(bb_collection_count(product, &count) == BB_STATUS_OK && count == 304);
  BB_TEST_ASSERT(allocator_state.attempt_count == attempts_before_count);
  BB_TEST_ASSERT(bb_collection_item_width(product) == 2 && map_state.calls == 0);
  BB_TEST_ASSERT(bb_collection_get(product, 100, pair, 2) == BB_STATUS_OK);
  BB_TEST_ASSERT(pair[0].data.integer == 5 && pair[1].data.image == map_state.images[5]);

  BB_TEST_ASSERT(bb_collection_map(product, 1, map_product_artifact, &map_state, &artifacts) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_slice(artifacts, 100, 16, &slice) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_collection_count(slice, &count) == BB_STATUS_OK && count == 16);
  BB_TEST_ASSERT(map_state.calls == 0);
  BB_TEST_ASSERT(bb_collection_get(slice, 0, &artifact, 1) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(artifact.data.artifact.key, "c05-m05"));
  BB_TEST_ASSERT(map_state.calls == 1);
  map_state.calls = 0;
  for (index = 0; index < 16; index += 1) {
    bb_surface *surface = NULL;
    bb_const_image_view view;
    BB_TEST_ASSERT(
      bb_collection_render_artifact_with(
        context,
        slice,
        index,
        graph,
        render_probe_raster,
        &probe,
        &surface
      ) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_surface_get_const_view(surface, &view) == BB_STATUS_OK);
    BB_TEST_ASSERT(view.desc.width == 1 && view.desc.height == 1);
    BB_TEST_ASSERT(view.data[0] == (uint8_t)((100 + index) % 19) && view.data[3] == 255);
    bb_surface_destroy(surface);
  }
  BB_TEST_ASSERT(probe.calls == 16 && map_state.calls == 16);

  bb_collection_release(slice);
  bb_collection_release(artifacts);
  bb_collection_release(product);
  bb_collection_release(masks);
  bb_collection_release(colors);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  BB_TEST_ASSERT(allocator_state.outstanding_allocations == 0);
  BB_TEST_ASSERT(allocator_state.outstanding_bytes == 0);
  return 1;
}

static bb_status flat_huge_count(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint64_t *out_count
) {
  (void)input;
  (void)input_count;
  *out_count = *(const uint64_t *)user;
  return BB_STATUS_OK;
}

static int test_collection_limits_and_synthetic_huge_product(void) {
  {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_collection *left = NULL;
    bb_collection *right = NULL;
    bb_collection *product = NULL;
    bb_value left_values[16];
    bb_value right_values[19];
    uint64_t count;
    size_t attempts;
    size_t index;
    desc.limits.max_collection_cardinality = 100;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    for (index = 0; index < 16; index += 1) left_values[index] = bb_integer((int64_t)index);
    for (index = 0; index < 19; index += 1) right_values[index] = bb_integer((int64_t)index);
    BB_TEST_ASSERT(bb_collection_from_values(context, left_values, 16, &left) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_from_values(context, right_values, 19, &right) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_product(left, right, &product) == BB_STATUS_OK);
    attempts = state.attempt_count;
    BB_TEST_ASSERT(bb_collection_count(product, &count) == BB_STATUS_LIMIT_EXCEEDED);
    BB_TEST_ASSERT(state.attempt_count == attempts);
    bb_collection_release(product);
    bb_collection_release(right);
    bb_collection_release(left);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  {
    bb_test_allocator_state state = {0};
    bb_context_desc desc = bb_test_context_desc(&state);
    bb_context *context = NULL;
    bb_collection *one = NULL;
    bb_collection *two = NULL;
    bb_collection *huge = NULL;
    bb_collection *product = NULL;
    bb_value one_value = bb_integer(1);
    bb_value two_values[2] = {bb_integer(1), bb_integer(2)};
    uint64_t huge_count = UINT64_MAX;
    uint64_t count;
    size_t outstanding;
    desc.limits.max_collection_cardinality = UINT64_MAX;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_from_values(context, &one_value, 1, &one) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_from_values(context, two_values, 2, &two) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_flat_map(one, 1, flat_huge_count, flat_integer_get, &huge_count, &huge) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_product(huge, two, &product) == BB_STATUS_OK);
    outstanding = state.outstanding_allocations;
    BB_TEST_ASSERT(bb_collection_count(product, &count) == BB_STATUS_LIMIT_EXCEEDED);
    BB_TEST_ASSERT(state.outstanding_allocations == outstanding);
    bb_collection_release(product);
    bb_collection_release(huge);
    bb_collection_release(two);
    bb_collection_release(one);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

static int test_collection_depth_and_allocation_failures(void) {
  {
    bb_context_desc desc;
    bb_context *context = NULL;
    bb_collection *base = NULL;
    bb_collection *mapped = (bb_collection *)(uintptr_t)1;
    bb_value value = bb_integer(1);
    bb_context_desc_init(&desc);
    desc.limits.max_collection_depth = 1;
    BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_from_values(context, &value, 1, &base) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_collection_map(base, 1, map_double, NULL, &mapped) == BB_STATUS_LIMIT_EXCEEDED);
    BB_TEST_ASSERT(mapped == NULL);
    bb_collection_release(base);
    bb_context_destroy(context);
  }
  {
    size_t fail_at;
    for (fail_at = 2; fail_at <= 4; fail_at += 1) {
      bb_test_allocator_state state = {0};
      bb_context_desc desc = bb_test_context_desc(&state);
      bb_context *context = NULL;
      bb_collection *collection = (bb_collection *)(uintptr_t)1;
      bb_value value;
      memset(&value, 0, sizeof(value));
      value.kind = BB_VALUE_STRING;
      value.data.string = BB_TEST_STRING("owned");
      state.fail_at_attempt = fail_at;
      BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
      BB_TEST_ASSERT(bb_collection_from_values(context, &value, 1, &collection) == BB_STATUS_OUT_OF_MEMORY);
      BB_TEST_ASSERT(collection == NULL);
      bb_context_destroy(context);
      BB_TEST_ASSERT(state.outstanding_allocations == 0);
      BB_TEST_ASSERT(state.outstanding_bytes == 0);
    }
  }
  return 1;
}

const bb_test_case bb_collection_tests[] = {
  {"collection combinators and owned values", test_collection_combinators_and_owned_values},
  {"lazy 16x19 product slice and render gate", test_lazy_product_slice_and_render_gate},
  {"collection limits and synthetic huge product", test_collection_limits_and_synthetic_huge_product},
  {"collection depth and allocation failures", test_collection_depth_and_allocation_failures},
};

const size_t bb_collection_test_count = sizeof(bb_collection_tests) / sizeof(bb_collection_tests[0]);
