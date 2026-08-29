#include "test_support.h"

#include <binblock/backend.h>

#include "binblock_wii.h"

#include <string.h>

#include "../../integrations/wii/demo/source/suite_module.h"

typedef struct wii_upload_state {
  size_t calls;
  size_t bytes;
  uint8_t first[4];
} wii_upload_state;

static bb_status wii_test_upload(
  void *user,
  const bb_wii_texture_desc *texture,
  const uint8_t *gx_rgba8,
  size_t byte_length
) {
  wii_upload_state *state = user;
  if (texture == NULL || gx_rgba8 == NULL || byte_length != texture->byte_length) return BB_STATUS_INVALID_ARGUMENT;
  state->calls += 1;
  state->bytes = byte_length;
  memcpy(state->first, gx_rgba8, sizeof(state->first));
  return BB_STATUS_OK;
}

typedef struct backend_test_state {
  bb_context *context;
  size_t upload_count;
  size_t direct_count;
  bb_image_node uploaded_node;
} backend_test_state;

static int backend_surfaces_equal(const bb_surface *left, const bb_surface *right) {
  bb_const_image_view left_view;
  bb_const_image_view right_view;
  if (bb_surface_get_const_view(left, &left_view) != BB_STATUS_OK ||
      bb_surface_get_const_view(right, &right_view) != BB_STATUS_OK) return 0;
  return left_view.desc.width == right_view.desc.width && left_view.desc.height == right_view.desc.height &&
         left_view.data_length == right_view.data_length &&
         memcmp(left_view.data, right_view.data, left_view.data_length) == 0;
}

static bb_status backend_test_upload(
  void *user,
  bb_image_node replaced_node,
  bb_hash128 structural_hash,
  const bb_surface *surface
) {
  backend_test_state *state = user;
  bb_const_image_view view;
  (void)structural_hash;
  if (bb_surface_get_const_view(surface, &view) != BB_STATUS_OK || view.desc.width != 64 || view.desc.height != 64)
    return BB_STATUS_INVALID_ARGUMENT;
  state->upload_count += 1;
  state->uploaded_node = replaced_node;
  return BB_STATUS_OK;
}

static bb_status backend_test_render(
  void *user,
  const bb_image_graph *graph,
  bb_image_node root,
  bb_surface **out_readback
) {
  backend_test_state *state = user;
  state->direct_count += 1;
  return bb_image_graph_render_raster(state->context, graph, root, out_readback);
}

static int test_limited_backend_partitions_mixed_graph_and_matches_cpu(void) {
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node background;
  bb_image_node source;
  bb_image_node resized;
  bb_image_node root;
  bb_backend_interface backend;
  bb_render_plan_options options;
  bb_render_plan *plan = NULL;
  bb_render_plan_step step;
  backend_test_state state = {0};
  bb_surface *actual = NULL;
  bb_surface *expected = NULL;
  const uint64_t supported = bb_backend_node_kind_bit(BB_IMAGE_NODE_FILL) |
                             bb_backend_node_kind_bit(BB_IMAGE_NODE_COMPOSITE);
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 64, 64, (bb_rgba8){0, 0, 255, 255}, &background) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 32, 32, (bb_rgba8){255, 0, 0, 255}, &source) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_resize(graph, source, 64, 64, &resized) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_composite(graph, background, resized, 0, 0, 0.5, &root) == BB_STATUS_OK);
  bb_backend_interface_init(&backend);
  backend.user = &state;
  backend.capabilities.flags = BB_BACKEND_CAN_UPLOAD | BB_BACKEND_CAN_READBACK;
  backend.capabilities.supported_node_kinds = supported;
  backend.capabilities.exact_node_kinds = supported;
  backend.upload_baked = backend_test_upload;
  backend.render_direct = backend_test_render;
  bb_render_plan_options_init(&options);
  state.context = context;
  BB_TEST_ASSERT(bb_render_plan_create(context, graph, root, &backend, &options, &plan) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_render_plan_step_count(plan) == 2);
  BB_TEST_ASSERT(bb_render_plan_step_get(plan, 0, &step) == BB_STATUS_OK);
  BB_TEST_ASSERT(step.kind == BB_RENDER_PLAN_CPU_BAKE_UPLOAD && step.root == resized);
  BB_TEST_ASSERT(bb_render_plan_step_get(plan, 1, &step) == BB_STATUS_OK);
  BB_TEST_ASSERT(step.kind == BB_RENDER_PLAN_BACKEND_DIRECT && step.root == root);
  BB_TEST_ASSERT(bb_render_plan_equivalence(plan) == BB_BACKEND_EQUIVALENCE_EXACT);
  BB_TEST_ASSERT(bb_render_plan_baked_bytes(plan) == 64 * 64 * 4);
  BB_TEST_ASSERT(bb_render_plan_execute(context, graph, plan, &backend, NULL, NULL, &actual) == BB_STATUS_OK);
  BB_TEST_ASSERT(state.upload_count == 1 && state.uploaded_node == resized && state.direct_count == 1);
  BB_TEST_ASSERT(bb_image_graph_render_raster(context, graph, root, &expected) == BB_STATUS_OK);
  BB_TEST_ASSERT(backend_surfaces_equal(actual, expected));
  bb_surface_destroy(expected);
  bb_surface_destroy(actual);
  bb_render_plan_destroy(plan);

  options.allow_cpu_fallback = 0;
  BB_TEST_ASSERT(bb_render_plan_create(context, graph, root, &backend, &options, &plan) == BB_STATUS_UNSUPPORTED);
  BB_TEST_ASSERT(plan == NULL);
  options.allow_cpu_fallback = 1;
  options.max_baked_bytes = 64 * 64 * 4 - 1;
  BB_TEST_ASSERT(bb_render_plan_create(context, graph, root, &backend, &options, &plan) == BB_STATUS_LIMIT_EXCEEDED);
  BB_TEST_ASSERT(plan == NULL);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 1;
}

static int test_unsupported_root_becomes_cpu_final_step(void) {
  bb_context *context = NULL;
  bb_image_graph *graph = NULL;
  bb_image_node fill;
  bb_image_node root;
  bb_backend_interface backend;
  bb_render_plan_options options;
  bb_render_plan *plan = NULL;
  bb_render_plan_step step;
  bb_surface *surface = NULL;
  bb_const_image_view view;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_create(context, &graph) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_fill(graph, 2, 2, (bb_rgba8){1, 2, 3, 255}, &fill) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_image_graph_add_resize(graph, fill, 4, 4, &root) == BB_STATUS_OK);
  bb_backend_interface_init(&backend);
  backend.capabilities.supported_node_kinds = bb_backend_node_kind_bit(BB_IMAGE_NODE_FILL);
  backend.capabilities.exact_node_kinds = backend.capabilities.supported_node_kinds;
  bb_render_plan_options_init(&options);
  BB_TEST_ASSERT(bb_render_plan_create(context, graph, root, &backend, &options, &plan) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_render_plan_step_count(plan) == 1);
  BB_TEST_ASSERT(bb_render_plan_step_get(plan, 0, &step) == BB_STATUS_OK);
  BB_TEST_ASSERT(step.kind == BB_RENDER_PLAN_CPU_FINAL && step.root == root);
  BB_TEST_ASSERT(bb_render_plan_execute(context, graph, plan, &backend, NULL, NULL, &surface) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_get_const_view(surface, &view) == BB_STATUS_OK);
  BB_TEST_ASSERT(view.desc.width == 4 && view.desc.height == 4 && view.data[0] == 1 && view.data[3] == 255);
  bb_surface_destroy(surface);
  bb_render_plan_destroy(plan);
  bb_image_graph_destroy(graph);
  bb_context_destroy(context);
  return 1;
}

static int test_platform_profiles_and_wii_rgba8_tile_layout(void) {
  const uint8_t pixels[] = {0x11, 0x22, 0x33, 0x44};
  const bb_const_image_view source = {
    {1, 1, 4, BB_PIXEL_FORMAT_RGBA8_UNORM, BB_COLOR_SPACE_NUMERIC_SRGB, BB_ALPHA_MODE_STRAIGHT},
    pixels,
    sizeof(pixels),
  };
  bb_backend_capabilities capabilities;
  bb_wii_texture_desc desc;
  uint8_t encoded[64];
  bb_backend_capabilities_webgl2(&capabilities);
  BB_TEST_ASSERT(capabilities.max_texture_width == 16384 && capabilities.bounded_max_channel_error == 1);
  BB_TEST_ASSERT((capabilities.supported_node_kinds & bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE)) == 0);
  bb_backend_capabilities_webgpu(&capabilities);
  BB_TEST_ASSERT((capabilities.supported_node_kinds & bb_backend_node_kind_bit(BB_IMAGE_NODE_RESIZE)) != 0);
  bb_backend_capabilities_wii_gx(&capabilities);
  BB_TEST_ASSERT(capabilities.max_texture_width == 1024 && capabilities.max_resources == 8);
  BB_TEST_ASSERT(capabilities.preferred_tile_width == 4 && capabilities.row_alignment == 32);
  BB_TEST_ASSERT(bb_wii_rgba8_texture_measure(1, 1, &desc) == BB_STATUS_OK);
  BB_TEST_ASSERT(desc.padded_width == 4 && desc.padded_height == 4 && desc.byte_length == 64);
  BB_TEST_ASSERT(bb_wii_rgba8_texture_encode(source, encoded, sizeof(encoded), &desc) == BB_STATUS_OK);
  BB_TEST_ASSERT(encoded[0] == 0x44 && encoded[1] == 0x11);
  BB_TEST_ASSERT(encoded[32] == 0x22 && encoded[33] == 0x33);
  BB_TEST_ASSERT(encoded[2] == 0 && encoded[34] == 0);
  return 1;
}

static int test_wii_host_loads_precompiled_program_and_uploads_baked_gx_texture(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "fill(#11223344).size(4)\n";
  uint8_t module[256];
  size_t module_length = 0;
  bb_wii_program *program = NULL;
  bb_program_output_info output;
  bb_wii_texture_desc texture;
  uint8_t scratch[64];
  wii_upload_state upload = {0};
  BB_TEST_ASSERT(
    bb_precompiled_module_write(
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      module,
      sizeof(module),
      &module_length
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(
    bb_wii_program_load(NULL, (bb_bytes){module, module_length}, NULL, &program) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wii_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_wii_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_wii_program_output(program, 0, &output) == BB_STATUS_OK && output.cardinality == 1);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 0, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 4 && texture.height == 4 && texture.byte_length == sizeof(scratch));
  BB_TEST_ASSERT(
    bb_wii_program_render_and_upload(
      program,
      0,
      0,
      scratch,
      sizeof(scratch),
      wii_test_upload,
      &upload,
      &texture
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(upload.calls == 1 && upload.bytes == 64);
  BB_TEST_ASSERT(upload.first[0] == 0x44 && upload.first[1] == 0x11);
  bb_wii_program_destroy(program);
  return 1;
}

static int test_wii_demo_suite_module_is_valid_and_varied(void) {
  bb_wii_program *program = NULL;
  bb_program_output_info output;
  bb_wii_texture_desc texture;
  uint8_t scratch[64 * 64 * 4];
  wii_upload_state upload = {0};
  BB_TEST_ASSERT(
    bb_wii_program_load(
      NULL,
      (bb_bytes){bb_wii_demo_module, BB_WII_DEMO_MODULE_BYTES},
      NULL,
      &program
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wii_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_wii_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_wii_program_output(program, 0, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(output.item_type == BB_SEMANTIC_ARTIFACT && output.cardinality == 12);

  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 0, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 64 && texture.height == 64 && texture.byte_length == sizeof(scratch));
  BB_TEST_ASSERT(
    bb_wii_program_render_and_upload(
      program,
      0,
      0,
      scratch,
      sizeof(scratch),
      wii_test_upload,
      &upload,
      &texture
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(upload.calls == 1 && upload.bytes == sizeof(scratch));

  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 4, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 72 && texture.height == 48);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 5, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 48 && texture.height == 72);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 8, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 80 && texture.height == 32);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 10, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 56 && texture.height == 72);
  BB_TEST_ASSERT(bb_wii_program_measure_output_texture(program, 0, 11, &texture) == BB_STATUS_OK);
  BB_TEST_ASSERT(texture.width == 64 && texture.height == 64);
  bb_wii_program_destroy(program);
  return 1;
}

const bb_test_case bb_backend_tests[] = {
  {"limited backend mixed graph partition and CPU equivalence", test_limited_backend_partitions_mixed_graph_and_matches_cpu},
  {"unsupported backend root uses CPU final", test_unsupported_root_becomes_cpu_final_step},
  {"platform profiles and Wii RGBA8 tile layout", test_platform_profiles_and_wii_rgba8_tile_layout},
  {"Wii host loads BBM and uploads baked GX texture", test_wii_host_loads_precompiled_program_and_uploads_baked_gx_texture},
  {"Wii demo suite BBM is valid and varied", test_wii_demo_suite_module_is_valid_and_varied},
};

const size_t bb_backend_test_count = sizeof(bb_backend_tests) / sizeof(bb_backend_tests[0]);
