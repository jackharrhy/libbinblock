#include "test_support.h"

#include <binblock/graph.h>

#include "context_internal.h"

#include <string.h>

static int graph_surface_equal(const bb_surface *left, const bb_surface *right) {
  bb_const_image_view left_view;
  bb_const_image_view right_view;
  if (bb_surface_get_const_view(left, &left_view) != BB_STATUS_OK ||
      bb_surface_get_const_view(right, &right_view) != BB_STATUS_OK) return 0;
  return left_view.desc.width == right_view.desc.width && left_view.desc.height == right_view.desc.height &&
         left_view.data_length == right_view.data_length && memcmp(left_view.data, right_view.data, left_view.data_length) == 0;
}

static int test_direct_graph_is_deduplicated_provenanced_and_renderable(void) {
  const bb_gradient_stop stops[] = {
    {0.0, {0, 0, 0, 255}, BB_EASING_LINEAR, 0},
    {1.0, {0, 0, 0, 0}, BB_EASING_LINEAR, 0},
  };
  const bb_linear_gradient_desc gradient = {4, 4, 180.0, 0.0, 0, stops, 2, BB_EASING_LINEAR};
  bb_context_desc render_desc;
  bb_context *graph_context = NULL;
  bb_context *render_context = NULL;
  bb_source_id source_id;
  bb_image_graph *graph = NULL;
  bb_image_node base;
  bb_image_node duplicate_base;
  bb_image_node unrelated;
  bb_image_node overlay;
  bb_image_node mask;
  bb_image_node masked;
  bb_image_node composited;
  bb_image_node root;
  bb_hash128 before;
  bb_hash128 after;
  bb_span span;
  bb_surface *actual = NULL;
  bb_surface *manual_base = NULL;
  bb_surface *manual_overlay = NULL;
  bb_surface *manual_mask = NULL;
  bb_surface *manual_masked = NULL;
  bb_surface *manual_composite = NULL;
  bb_surface *expected = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &graph_context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_context_add_source(graph_context, BB_TEST_STRING("graph.bb"), BB_TEST_BYTES("base overlay"), &source_id) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(graph_context, &graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 4, 4, (bb_rgba8){0, 0, 255, 255}, &base) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 4, 4, (bb_rgba8){0, 0, 255, 255}, &duplicate_base) == BB_STATUS_OK);
  BB_TEST_ASSERT(base == duplicate_base && bb_image_graph_node_count(graph) == 1);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 1000, 1000, (bb_rgba8){1, 2, 3, 4}, &unrelated) == BB_STATUS_OK);
  BB_TEST_ASSERT(unrelated != BB_IMAGE_NODE_NONE);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 4, 4, (bb_rgba8){255, 0, 0, 255}, &overlay) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_linear_gradient(graph, &gradient, &mask) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_mask(graph, overlay, mask, BB_MASK_REPLACE, &masked) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_composite(graph, base, masked, 0, 0, 1.0, &composited) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_resize(graph, composited, 2, 2, &root) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_node_hash(graph, base, &before) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_attach_span(graph, base, (bb_span){source_id, 0, 4}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_attach_span(graph, base, (bb_span){source_id, 5, 12}) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_provenance_count(graph, base) == 2);
  BB_TEST_ASSERT(bb_image_graph_provenance(graph, base, 1, &span) == BB_STATUS_OK);
  BB_TEST_ASSERT(span.byte_start == 5 && span.byte_end == 12);
  BB_TEST_ASSERT(bb_image_graph_node_hash(graph, base, &after) == BB_STATUS_OK);
  BB_TEST_ASSERT(before.low == after.low && before.high == after.high);
  BB_TEST_ASSERT(bb_image_graph_seal(graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 1, 1, (bb_rgba8){0, 0, 0, 0}, &duplicate_base) == BB_STATUS_INVALID_ARGUMENT);

  bb_context_desc_init(&render_desc);
  render_desc.limits.max_total_allocation_bytes = 64 * 1024;
  BB_TEST_ASSERT(bb_context_create(&render_desc, &render_context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_render_raster(render_context, graph, root, &actual) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_fill(render_context, 4, 4, (bb_rgba8){0, 0, 255, 255}, &manual_base) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_fill(render_context, 4, 4, (bb_rgba8){255, 0, 0, 255}, &manual_overlay) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_linear_gradient(render_context, &gradient, &manual_mask) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_apply_mask(render_context, manual_overlay, manual_mask, BB_MASK_REPLACE, &manual_masked) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_source_over(render_context, manual_base, manual_masked, 0, 0, 1.0, &manual_composite) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_resize_lanczos3(render_context, manual_composite, 2, 2, &expected) == BB_STATUS_OK);
  BB_TEST_ASSERT(graph_surface_equal(actual, expected));
  bb_surface_destroy(expected);
  bb_surface_destroy(manual_composite);
  bb_surface_destroy(manual_masked);
  bb_surface_destroy(manual_mask);
  bb_surface_destroy(manual_overlay);
  bb_surface_destroy(manual_base);
  bb_surface_destroy(actual);
  bb_context_destroy(render_context);
  bb_image_graph_destroy(graph);
  bb_context_destroy(graph_context);
  return 1;
}

static int test_graph_hashes_are_deterministic_across_graphs(void) {
  bb_context *context = NULL;
  bb_image_graph *left = NULL;
  bb_image_graph *right = NULL;
  bb_image_node left_node;
  bb_image_node right_node;
  bb_hash128 left_hash;
  bb_hash128 right_hash;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &left) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &right) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(left, 8, 9, (bb_rgba8){1, 2, 3, 4}, &left_node) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(right, 8, 9, (bb_rgba8){1, 2, 3, 4}, &right_node) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_node_hash(left, left_node, &left_hash) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_node_hash(right, right_node, &right_hash) == BB_STATUS_OK);
  BB_TEST_ASSERT(left_hash.low == right_hash.low && left_hash.high == right_hash.high);
  bb_image_graph_destroy(right);
  bb_image_graph_destroy(left);
  bb_context_destroy(context);
  return 1;
}

static int test_graph_limits_and_allocation_failures(void) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node fill;
  bb_image_node opacity;
  bb_context_desc_init(&desc);
  desc.limits.max_graph_depth = 1;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 1, 1, (bb_rgba8){0, 0, 0, 255}, &fill) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_opacity(graph, fill, 0.5, &opacity) == BB_STATUS_LIMIT_EXCEEDED);
  BB_TEST_ASSERT(bb_image_graph_node_count(graph) == 1);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);

  {
    bb_test_allocator_state state = {0};
    bb_context_desc failure_desc = bb_test_context_desc(&state);
    state.fail_at_attempt = 2;
    BB_TEST_ASSERT(bb_context_create(&failure_desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(graph == NULL);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  {
    bb_test_allocator_state state = {0};
    bb_context_desc failure_desc = bb_test_context_desc(&state);
    state.fail_at_attempt = 3;
    BB_TEST_ASSERT(bb_context_create(&failure_desc, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 1, 1, (bb_rgba8){0, 0, 0, 255}, &fill) == BB_STATUS_OUT_OF_MEMORY);
    BB_TEST_ASSERT(fill == BB_IMAGE_NODE_NONE && bb_image_graph_node_count(graph) == 0);
    bb_image_graph_destroy(graph);
    bb_context_destroy(context);
    BB_TEST_ASSERT(state.outstanding_allocations == 0);
  }
  return 1;
}

static uint32_t test_render_cancel(void *user) {
  uint32_t *calls = user;
  *calls += 1;
  return 1;
}

static int test_render_work_pixel_and_cancellation_limits(void) {
  bb_context_desc desc;
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node fill;
  bb_image_node root;
  bb_render_options options;
  bb_surface *surface = NULL;
  uint32_t cancel_calls = 0;

  bb_context_desc_init(&desc);
  desc.limits.max_render_pixels = 3;
  BB_TEST_ASSERT(bb_context_create(&desc, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_fill(context, 2, 2, (bb_rgba8){1, 2, 3, 4}, &surface) == BB_STATUS_LIMIT_EXCEEDED);
  BB_TEST_ASSERT(surface == NULL);
  bb_context_destroy(context);

  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 2, 2, (bb_rgba8){1, 2, 3, 4}, &fill) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_opacity(graph, fill, 0.5, &root) == BB_STATUS_OK);
  bb_render_options_init(&options);
  options.max_work_units = 7;
  BB_TEST_ASSERT(
    bb_image_graph_render_raster_with_options(context, graph, root, &options, NULL, NULL, &surface) ==
    BB_STATUS_LIMIT_EXCEEDED
  );
  BB_TEST_ASSERT(surface == NULL);
  options.max_work_units = 8;
  BB_TEST_ASSERT(
    bb_image_graph_render_raster_with_options(context, graph, root, &options, NULL, NULL, &surface) == BB_STATUS_OK
  );
  bb_surface_destroy(surface);
  surface = NULL;
  options.user = &cancel_calls;
  options.should_cancel = test_render_cancel;
  BB_TEST_ASSERT(
    bb_image_graph_render_raster_with_options(context, graph, root, &options, NULL, NULL, &surface) ==
    BB_STATUS_CANCELLED
  );
  BB_TEST_ASSERT(cancel_calls == 1 && surface == NULL);
  options.struct_size = 0;
  BB_TEST_ASSERT(
    bb_image_graph_render_raster_with_options(context, graph, root, &options, NULL, NULL, &surface) ==
    BB_STATUS_INVALID_ARGUMENT
  );
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 1;
}

const bb_test_case bb_graph_tests[] = {
  {"direct graph construction and raster render", test_direct_graph_is_deduplicated_provenanced_and_renderable},
  {"deterministic graph hashes", test_graph_hashes_are_deterministic_across_graphs},
  {"graph limits and allocation failures", test_graph_limits_and_allocation_failures},
  {"render work, pixel, and cancellation limits", test_render_work_pixel_and_cancellation_limits},
};

const size_t bb_graph_test_count = sizeof(bb_graph_tests) / sizeof(bb_graph_tests[0]);
