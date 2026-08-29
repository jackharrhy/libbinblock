#include "test_support.h"

#include <binblock/program.h>
#include <binblock/module.h>
#include <binblock/reference_set.h>

#include <stdint.h>
#include <string.h>

static int program_surfaces_equal(const bb_surface *left, const bb_surface *right) {
  bb_const_image_view left_view;
  bb_const_image_view right_view;
  if (bb_surface_get_const_view(left, &left_view) != BB_STATUS_OK ||
      bb_surface_get_const_view(right, &right_view) != BB_STATUS_OK) return 0;
  return left_view.desc.width == right_view.desc.width && left_view.desc.height == right_view.desc.height &&
         left_view.data_length == right_view.data_length &&
         memcmp(left_view.data, right_view.data, left_view.data_length) == 0;
}

static int test_starter_program_compiles_lazily_and_renders_exactly(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "\n"
    "size := 64\n"
    "colors := palette(red: #ff0000, blue: #0000ff)\n"
    "blocks := colors.map(fill).size(size)\n"
    "fade := lg(180deg, white, transparent-white).size(size)\n"
    "outputs := blocks.mask(fade)\n"
    "outputs\n";
  const bb_gradient_stop stops[] = {
    {0.0, {255, 255, 255, 255}, BB_EASING_LINEAR, 0},
    {1.0, {255, 255, 255, 0}, BB_EASING_LINEAR, 0},
  };
  const bb_linear_gradient_desc gradient = {64, 64, 180.0, 0.0, 0, stops, 2, BB_EASING_LINEAR};
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_program_output_info output_info;
  bb_artifact_value artifact;
  bb_semantic_trace trace;
  bb_surface *actual = NULL;
  bb_surface *base = NULL;
  bb_surface *mask = NULL;
  bb_surface *expected = NULL;
  const char *angle_text = strstr(source, "180deg");
  const char *output_text = strrchr(source, 'o');
  uint32_t graph_nodes_before;
  BB_TEST_ASSERT(angle_text != NULL && output_text != NULL);
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("starter.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile(context, syntax, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_program_output(program, 0, &output_info) == BB_STATUS_OK);
  BB_TEST_ASSERT(output_info.item_type == BB_SEMANTIC_ARTIFACT && output_info.cardinality == 2);
  graph_nodes_before = bb_image_graph_node_count(bb_program_image_graph(program));
  BB_TEST_ASSERT(graph_nodes_before == 1);

  BB_TEST_ASSERT(
    bb_program_trace_at(program, source_id, (uint32_t)(angle_text - source), &trace) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(trace.type == BB_SEMANTIC_DEGREES);
  BB_TEST_ASSERT(
    bb_program_trace_at(program, source_id, (uint32_t)(output_text - source), &trace) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(trace.type == BB_SEMANTIC_COLLECTION && trace.output_index == 0);

  bb_syntax_tree_destroy(syntax);
  syntax = NULL;
  BB_TEST_ASSERT(bb_program_output_artifact(program, 0, 0, &artifact) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(artifact.key, "red"));
  BB_TEST_ASSERT(bb_test_string_equal(artifact.path, "red"));
  BB_TEST_ASSERT(artifact.image != BB_IMAGE_NODE_NONE);
  BB_TEST_ASSERT(bb_image_graph_node_count(bb_program_image_graph(program)) == graph_nodes_before + 3);
  BB_TEST_ASSERT(bb_program_render_output(program, 0, 0, &actual) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_fill(context, 64, 64, (bb_rgba8){255, 0, 0, 255}, &base) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_linear_gradient(context, &gradient, &mask) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_apply_mask(context, base, mask, BB_MASK_REPLACE, &expected) == BB_STATUS_OK);
  BB_TEST_ASSERT(program_surfaces_equal(actual, expected));
  BB_TEST_ASSERT(bb_image_graph_node_count(bb_program_image_graph(program)) == graph_nodes_before + 3);
  bb_surface_destroy(expected);
  bb_surface_destroy(mask);
  bb_surface_destroy(base);
  bb_surface_destroy(actual);
  BB_TEST_ASSERT(bb_program_render_output(program, 0, 2, &actual) == BB_STATUS_NOT_FOUND);
  BB_TEST_ASSERT(actual == NULL);
  bb_program_destroy(program);
  bb_context_destroy(context);
  return 1;
}

static int test_reference_module_lowers_analytic_alpha_map_with_artifact_path(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "import \"binblock/reference-set\"\n"
    "artifact(\"Gradient Layers Alpha Maps/13.png\", reference-alpha-map(13))\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_artifact_value artifact;
  bb_reference_alpha_map reference;
  bb_surface *actual = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("reference-alpha.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile(context, syntax, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_program_output_artifact(program, 0, 0, &artifact) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_test_string_equal(artifact.key, "Gradient Layers Alpha Maps/13.png"));
  BB_TEST_ASSERT(bb_test_string_equal(artifact.path, "Gradient Layers Alpha Maps/13.png"));
  BB_TEST_ASSERT(bb_program_render_output(program, 0, 0, &actual) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_reference_set_alpha_map(context, 13, NULL, &reference) == BB_STATUS_OK);
  BB_TEST_ASSERT(reference.offset_x == 1 && reference.offset_y == 1);
  BB_TEST_ASSERT(program_surfaces_equal(actual, reference.surface));
  bb_surface_destroy(reference.surface);
  bb_surface_destroy(actual);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static int test_fluent_image_operations_lower_to_canonical_graph(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "base := fill(#010203).size(3, 2)\n"
    "top := fill(#ff000080).size(2, 1).opacity(50%)\n"
    "base.over(top, 100%).crop(-1, 0, 4, 2).canvas(5, 3, 1, 1).rotate(1)\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_artifact_value artifact;
  bb_image_node_info root_info;
  bb_surface *actual = NULL;
  bb_surface *base_pixel = NULL;
  bb_surface *base = NULL;
  bb_surface *top_pixel = NULL;
  bb_surface *top = NULL;
  bb_surface *faded = NULL;
  bb_surface *composite = NULL;
  bb_surface *cropped = NULL;
  bb_surface *canvas = NULL;
  bb_surface *expected = NULL;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("fluent-images.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile(context, syntax, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0 && bb_program_output_count(program) == 1);
  BB_TEST_ASSERT(bb_program_output_artifact(program, 0, 0, &artifact) == BB_STATUS_OK);
  memset(&root_info, 0, sizeof(root_info));
  root_info.struct_size = sizeof(root_info);
  BB_TEST_ASSERT(
    bb_image_graph_node_info(bb_program_image_graph(program), artifact.image, &root_info) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(root_info.kind == BB_IMAGE_NODE_ROTATE && root_info.width == 3 && root_info.height == 5);
  BB_TEST_ASSERT(bb_program_render_output(program, 0, 0, &actual) == BB_STATUS_OK);

  BB_TEST_ASSERT(bb_raster_fill(context, 1, 1, (bb_rgba8){1, 2, 3, 255}, &base_pixel) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_resize_lanczos3(context, base_pixel, 3, 2, &base) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_fill(context, 1, 1, (bb_rgba8){255, 0, 0, 128}, &top_pixel) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_resize_lanczos3(context, top_pixel, 2, 1, &top) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_opacity(context, top, 0.5, &faded) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_source_over(context, base, faded, 0, 0, 1.0, &composite) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_crop(context, composite, -1, 0, 4, 2, &cropped) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_canvas(context, cropped, 5, 3, 1, 1, &canvas) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_raster_rotate_quarter_turns(context, canvas, 1, &expected) == BB_STATUS_OK);
  BB_TEST_ASSERT(program_surfaces_equal(actual, expected));

  bb_surface_destroy(expected);
  bb_surface_destroy(canvas);
  bb_surface_destroy(cropped);
  bb_surface_destroy(composite);
  bb_surface_destroy(faded);
  bb_surface_destroy(top);
  bb_surface_destroy(top_pixel);
  bb_surface_destroy(base);
  bb_surface_destroy(base_pixel);
  bb_surface_destroy(actual);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static int test_radial_vector_color_transforms_and_unary_collection_lifting(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "field := rg(#000000a3, #00000000, center: [1.5, 1.5], radius: 2, width: 4, height: 4, easing: \"legacy\", legacy-rounding: true)\n"
    "field.rgb(#ffffff).invert-alpha().tint(#00ff00).remap(#00ff00, #000000, #ff0000, #0000ff).shift-rgb(#ff0000, #00ffff)\n"
    "items := palette(red: #ff0000, blue: #0000ff).map(fill).size(2).opacity(50%).rotate(1).crop(0, 0, 1, 1).canvas(2, 2, 1, 1).rgb(#ffffff).invert-alpha()\n"
    "items\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_program_output_info output;
  bb_artifact_value artifact;
  bb_image_node_info node_info;
  bb_semantic_trace trace;
  bb_surface *surface = NULL;
  const char *vector_text = strstr(source, "[1.5");
  BB_TEST_ASSERT(vector_text != NULL);
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("generic-transforms.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile(context, syntax, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0 && bb_program_output_count(program) == 2);
  BB_TEST_ASSERT(
    bb_program_trace_at(program, source_id, (uint32_t)(vector_text - source), &trace) == BB_STATUS_OK &&
    trace.type == BB_SEMANTIC_VECTOR2
  );
  BB_TEST_ASSERT(bb_program_output(program, 0, &output) == BB_STATUS_OK && output.cardinality == 1);
  BB_TEST_ASSERT(bb_program_output_artifact(program, 0, 0, &artifact) == BB_STATUS_OK);
  memset(&node_info, 0, sizeof(node_info));
  node_info.struct_size = sizeof(node_info);
  BB_TEST_ASSERT(bb_image_graph_node_info(bb_program_image_graph(program), artifact.image, &node_info) == BB_STATUS_OK);
  BB_TEST_ASSERT(node_info.kind == BB_IMAGE_NODE_SHIFT_RGB && node_info.width == 4 && node_info.height == 4);
  BB_TEST_ASSERT(bb_program_render_output(program, 0, 0, &surface) == BB_STATUS_OK);
  bb_surface_destroy(surface);
  surface = NULL;
  BB_TEST_ASSERT(bb_program_output(program, 1, &output) == BB_STATUS_OK && output.cardinality == 2);
  BB_TEST_ASSERT(bb_program_output_artifact(program, 1, 1, &artifact) == BB_STATUS_OK);
  memset(&node_info, 0, sizeof(node_info));
  node_info.struct_size = sizeof(node_info);
  BB_TEST_ASSERT(bb_image_graph_node_info(bb_program_image_graph(program), artifact.image, &node_info) == BB_STATUS_OK);
  BB_TEST_ASSERT(node_info.kind == BB_IMAGE_NODE_INVERT_ALPHA && node_info.width == 2 && node_info.height == 2);
  BB_TEST_ASSERT(bb_program_render_output(program, 1, 1, &surface) == BB_STATUS_OK);
  bb_surface_destroy(surface);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static int test_scalar_parameter_override_recompiles_graph_shape(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "size := 2\n"
    "artifact(\"red.png\", fill(#ff0000).size(size))\n";
  const bb_parameter_override override = {
    BB_TEST_STRING("size"),
    BB_SEMANTIC_INTEGER,
    {.integer = 4},
  };
  bb_compile_options options;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_program_parameter_info parameter;
  bb_surface *surface = NULL;
  bb_const_image_view view;
  bb_compile_options_init(&options);
  options.parameter_overrides = &override;
  options.parameter_override_count = 1;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("override.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile_with_options(context, syntax, &options, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_program_parameter_count(program) == 1);
  BB_TEST_ASSERT(bb_program_parameter(program, 0, &parameter) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_test_string_equal(parameter.name, "size") && parameter.type == BB_SEMANTIC_INTEGER &&
    parameter.value.integer == 4
  );
  BB_TEST_ASSERT(bb_program_render_output(program, 0, 0, &surface) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_surface_get_const_view(surface, &view) == BB_STATUS_OK);
  BB_TEST_ASSERT(view.desc.width == 4 && view.desc.height == 4);
  bb_surface_destroy(surface);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static int test_semantic_diagnostics_cover_imports_duplicates_cycles_and_types(void) {
  static const char source[] =
    "import \"missing/module\"\n"
    "a := b\n"
    "b := a\n"
    "a := 2\n"
    "a.size(\"bad\")\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  size_t index;
  int saw_import = 0;
  int saw_duplicate = 0;
  int saw_cycle = 0;
  int saw_type = 0;
  uint32_t previous_start = 0;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("errors.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile(context, syntax, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) >= 4);
  for (index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    BB_TEST_ASSERT(bb_program_diagnostic(program, index, &diagnostic) == BB_STATUS_OK);
    BB_TEST_ASSERT(index == 0 || diagnostic.primary_span.byte_start >= previous_start);
    previous_start = diagnostic.primary_span.byte_start;
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT) saw_import = 1;
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_DUPLICATE_BINDING) saw_duplicate = 1;
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_BINDING_CYCLE) saw_cycle = 1;
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH) saw_type = 1;
  }
  BB_TEST_ASSERT(saw_import && saw_duplicate && saw_cycle && saw_type);
  BB_TEST_ASSERT(bb_program_output_count(program) == 0);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static int test_collection_collection_mask_requires_equal_zip_lengths(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "left := palette(red: #ff0000, blue: #0000ff).map(fill).size(2)\n"
    "right := palette(a: #ffffff, b: #ffffff, c: #ffffff).map(fill).size(2)\n"
    "left.mask(right)\n";
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  size_t index;
  int saw_cardinality = 0;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("zip.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile(context, syntax, &program) == BB_STATUS_OK);
  for (index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    BB_TEST_ASSERT(bb_program_diagnostic(program, index, &diagnostic) == BB_STATUS_OK);
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_CARDINALITY) saw_cardinality = 1;
  }
  BB_TEST_ASSERT(saw_cardinality && bb_program_output_count(program) == 0);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

typedef struct resolver_test_state {
  int adapter_kind;
  size_t module_calls;
  size_t asset_calls;
  size_t decode_calls;
  size_t encoded_calls;
  int cycle_mode;
  int reuse_dependency_storage_mode;
  uint8_t precompiled[64];
  size_t precompiled_length;
} resolver_test_state;

static int resolver_view_is(bb_string_view value, const char *expected) {
  return bb_test_string_equal(value, expected);
}

static bb_status test_module_resolver(
  void *user,
  const bb_module_request *request,
  bb_resolved_module *out_module
) {
  static const uint8_t theme_source[] = "import \"host/shared\"\n";
  static const uint8_t module_a[] = "import \"B\"\n";
  static const uint8_t module_b[] = "import \"A\"\n";
  resolver_test_state *state = user;
  state->module_calls += 1;
  memset(out_module, 0, sizeof(*out_module));
  if (state->cycle_mode && resolver_view_is(request->specifier, "A")) {
    out_module->kind = BB_RESOLVED_MODULE_SOURCE;
    out_module->identity = BB_TEST_STRING("module:A:v1");
    out_module->source_name = BB_TEST_STRING("A.bb");
    out_module->source = (bb_bytes){module_a, sizeof(module_a) - 1};
    return BB_STATUS_OK;
  }
  if (state->cycle_mode && resolver_view_is(request->specifier, "B")) {
    out_module->kind = BB_RESOLVED_MODULE_SOURCE;
    out_module->identity = BB_TEST_STRING("module:B:v1");
    out_module->source_name = BB_TEST_STRING("B.bb");
    out_module->source = (bb_bytes){module_b, sizeof(module_b) - 1};
    return BB_STATUS_OK;
  }
  if (resolver_view_is(request->specifier, "host/theme")) {
    out_module->kind = BB_RESOLVED_MODULE_SOURCE;
    out_module->identity = BB_TEST_STRING("sha256:theme-v1");
    out_module->source_name = BB_TEST_STRING("theme.bb");
    out_module->source = (bb_bytes){theme_source, sizeof(theme_source) - 1};
    return BB_STATUS_OK;
  }
  if (resolver_view_is(request->specifier, "host/shared")) {
    if (state->precompiled_length == 0 &&
        bb_precompiled_module_write(
          (bb_bytes){NULL, 0},
          state->precompiled,
          sizeof(state->precompiled),
          &state->precompiled_length
        ) != BB_STATUS_OK) return BB_STATUS_INTERNAL_ERROR;
    out_module->kind = BB_RESOLVED_MODULE_PRECOMPILED;
    out_module->identity = BB_TEST_STRING("sha256:shared-v1");
    out_module->precompiled = (bb_bytes){state->precompiled, state->precompiled_length};
    return BB_STATUS_OK;
  }
  return BB_STATUS_NOT_FOUND;
}

static bb_status test_asset_resolver(
  void *user,
  const bb_asset_request *request,
  bb_resolved_asset *out_asset
) {
  static const bb_string_view root_dependencies[] = {{"child", sizeof("child") - 1}};
  static const bb_string_view child_dependencies[] = {{"root", sizeof("root") - 1}};
  static bb_string_view reused_dependencies[2];
  resolver_test_state *state = user;
  state->asset_calls += 1;
  memset(out_asset, 0, sizeof(*out_asset));
  if (state->reuse_dependency_storage_mode) {
    if (resolver_view_is(request->logical_id, "root")) {
      reused_dependencies[0] = BB_TEST_STRING("child-1");
      reused_dependencies[1] = BB_TEST_STRING("child-2");
      out_asset->dependencies = reused_dependencies;
      out_asset->dependency_count = 2;
    } else if (resolver_view_is(request->logical_id, "child-1")) {
      reused_dependencies[1] = BB_TEST_STRING("invalidated-by-nested-call");
    } else if (!resolver_view_is(request->logical_id, "child-2")) return BB_STATUS_NOT_FOUND;
    out_asset->content_id = request->logical_id;
    out_asset->width = 1;
    out_asset->height = 1;
    return BB_STATUS_OK;
  }
  if (state->cycle_mode && resolver_view_is(request->logical_id, "root")) {
    out_asset->content_id = BB_TEST_STRING("sha256:root");
    out_asset->width = 1;
    out_asset->height = 1;
    out_asset->dependencies = root_dependencies;
    out_asset->dependency_count = 1;
    return BB_STATUS_OK;
  }
  if (state->cycle_mode && resolver_view_is(request->logical_id, "child")) {
    out_asset->content_id = BB_TEST_STRING("sha256:child");
    out_asset->width = 1;
    out_asset->height = 1;
    out_asset->dependencies = child_dependencies;
    out_asset->dependency_count = 1;
    return BB_STATUS_OK;
  }
  if (!resolver_view_is(request->logical_id, "logo")) return BB_STATUS_NOT_FOUND;
  out_asset->content_id = BB_TEST_STRING("sha256:logo");
  out_asset->width = 2;
  out_asset->height = 2;
  out_asset->has_encoded_bytes = 1;
  return BB_STATUS_OK;
}

static bb_status test_asset_decoder(
  void *user,
  bb_string_view content_id,
  bb_const_image_view *out_image
) {
  static const uint8_t pixels[] = {
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 0,
  };
  resolver_test_state *state = user;
  if (!resolver_view_is(content_id, "sha256:logo")) return BB_STATUS_NOT_FOUND;
  state->decode_calls += 1;
  out_image->desc = (bb_image_desc){
    2,
    2,
    8,
    BB_PIXEL_FORMAT_RGBA8_UNORM,
    BB_COLOR_SPACE_NUMERIC_SRGB,
    BB_ALPHA_MODE_STRAIGHT,
  };
  out_image->data = pixels;
  out_image->data_length = sizeof(pixels);
  return BB_STATUS_OK;
}

static bb_status test_asset_encoded(
  void *user,
  bb_string_view content_id,
  bb_bytes *out_bytes
) {
  static const uint8_t encoded[] = {0x89, 'P', 'N', 'G'};
  resolver_test_state *state = user;
  if (!resolver_view_is(content_id, "sha256:logo")) return BB_STATUS_NOT_FOUND;
  state->encoded_calls += 1;
  *out_bytes = (bb_bytes){encoded, sizeof(encoded)};
  return BB_STATUS_OK;
}

static int test_host_resolved_modules_assets_and_lazy_decode(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "import \"host/theme\"\n"
    "logo := asset(\"logo\", hash: \"sha256:logo\", width: 2, height: 2)\n"
    "logo\n";
  int adapter_kind;
  for (adapter_kind = 0; adapter_kind < 3; adapter_kind += 1) {
    resolver_test_state state = {0};
    bb_compile_options options;
    bb_context *context = NULL;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *syntax = NULL;
    bb_program *program = NULL;
    bb_artifact_value artifact;
    bb_bytes encoded;
    bb_surface *surface = NULL;
    bb_const_image_view view;
    state.adapter_kind = adapter_kind;
    bb_compile_options_init(&options);
    options.user = &state;
    options.resolve_module = test_module_resolver;
    options.resolve_asset = test_asset_resolver;
    options.decode_asset = test_asset_decoder;
    options.encoded_asset = test_asset_encoded;
    BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
    BB_TEST_ASSERT(
      bb_context_add_source(
        context,
        BB_TEST_STRING("resolved.bb"),
        (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
        &source_id
      ) == BB_STATUS_OK
    );
    BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_program_compile_with_options(context, syntax, &options, &program) == BB_STATUS_OK);
    BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0);
    BB_TEST_ASSERT(state.module_calls == 2 && state.asset_calls == 1 && state.decode_calls == 0);
    BB_TEST_ASSERT(bb_program_output_count(program) == 1);
    BB_TEST_ASSERT(bb_image_graph_node_count(bb_program_image_graph(program)) == 1);
    bb_syntax_tree_destroy(syntax);
    BB_TEST_ASSERT(bb_program_output_artifact(program, 0, 0, &artifact) == BB_STATUS_OK);
    BB_TEST_ASSERT(artifact.alias_identity == BB_ALIAS_BYTES);
    BB_TEST_ASSERT(bb_test_string_equal(artifact.alias_target, "sha256:logo"));
    BB_TEST_ASSERT(bb_program_output_encoded(program, 0, 0, &encoded) == BB_STATUS_OK);
    BB_TEST_ASSERT(encoded.length == 4 && encoded.data[0] == 0x89 && state.encoded_calls == 1);
    BB_TEST_ASSERT(bb_program_render_output(program, 0, 0, &surface) == BB_STATUS_OK);
    BB_TEST_ASSERT(state.decode_calls == 1);
    BB_TEST_ASSERT(bb_surface_get_const_view(surface, &view) == BB_STATUS_OK);
    BB_TEST_ASSERT(view.desc.width == 2 && view.desc.height == 2 && view.data[0] == 255 && view.data[3] == 255);
    BB_TEST_ASSERT(view.data[12] == 255 && view.data[15] == 0);
    bb_surface_destroy(surface);
    bb_program_destroy(program);
    bb_context_destroy(context);
  }
  return 1;
}

static int test_module_and_asset_cycles_are_diagnostics(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "import \"A\"\n"
    "asset(\"root\")\n";
  resolver_test_state state = {0};
  bb_compile_options options;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  size_t index;
  int saw_module_cycle = 0;
  int saw_asset_cycle = 0;
  state.cycle_mode = 1;
  bb_compile_options_init(&options);
  options.user = &state;
  options.resolve_module = test_module_resolver;
  options.resolve_asset = test_asset_resolver;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("cycles.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile_with_options(context, syntax, &options, &program) == BB_STATUS_OK);
  for (index = 0; index < bb_program_diagnostic_count(program); index += 1) {
    bb_diagnostic diagnostic;
    BB_TEST_ASSERT(bb_program_diagnostic(program, index, &diagnostic) == BB_STATUS_OK);
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_MODULE_CYCLE) saw_module_cycle = 1;
    if (diagnostic.code == BB_SEMANTIC_DIAGNOSTIC_ASSET_CYCLE) saw_asset_cycle = 1;
  }
  BB_TEST_ASSERT(saw_module_cycle && saw_asset_cycle);
  BB_TEST_ASSERT(bb_program_output_count(program) == 0);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static int test_asset_dependency_views_are_copied_before_nested_resolution(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "asset(\"root\")\n";
  resolver_test_state state = {0};
  bb_compile_options options;
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  state.reuse_dependency_storage_mode = 1;
  bb_compile_options_init(&options);
  options.user = &state;
  options.resolve_asset = test_asset_resolver;
  BB_TEST_ASSERT(bb_context_create(NULL, &context) == BB_STATUS_OK);
  BB_TEST_ASSERT(
    bb_context_add_source(
      context,
      BB_TEST_STRING("dependency-lifetime.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_syntax_parse(context, source_id, &syntax) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_compile_with_options(context, syntax, &options, &program) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_program_diagnostic_count(program) == 0);
  BB_TEST_ASSERT(bb_program_output_count(program) == 1 && state.asset_calls == 3);
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  return 1;
}

static size_t test_compile_with_failure_at(size_t fail_at, int require_success, int *out_clean) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "size := 8\n"
    "colors := palette(red: #ff0000, blue: #0000ff)\n"
    "blocks := colors.map(fill).size(size)\n"
    "fade := lg(180deg, white, transparent-white).size(size)\n"
    "blocks.mask(fade)\n";
  bb_test_allocator_state state = {0};
  bb_context_desc desc = bb_test_context_desc(&state);
  bb_context *context = NULL;
  bb_source_id source_id = BB_SOURCE_ID_NONE;
  bb_syntax_tree *syntax = NULL;
  bb_program *program = NULL;
  bb_status status;
  state.fail_at_attempt = fail_at;
  status = bb_context_create(&desc, &context);
  if (status == BB_STATUS_OK)
    status = bb_context_add_source(
      context,
      BB_TEST_STRING("failure-sweep.bb"),
      (bb_bytes){(const uint8_t *)source, sizeof(source) - 1},
      &source_id
    );
  if (status == BB_STATUS_OK) status = bb_syntax_parse(context, source_id, &syntax);
  if (status == BB_STATUS_OK) status = bb_program_compile(context, syntax, &program);
  if (require_success && (status != BB_STATUS_OK || program == NULL || bb_program_diagnostic_count(program) != 0))
    *out_clean = 0;
  bb_program_destroy(program);
  bb_syntax_tree_destroy(syntax);
  bb_context_destroy(context);
  if (state.outstanding_allocations != 0 || state.outstanding_bytes != 0) *out_clean = 0;
  return state.attempt_count;
}

static int test_semantic_compile_allocation_failure_sweep(void) {
  size_t fail_at;
  int clean = 1;
  const size_t allocation_points = test_compile_with_failure_at(0, 1, &clean);
  BB_TEST_ASSERT(clean && allocation_points > 20);
  for (fail_at = 1; fail_at <= allocation_points; fail_at += 1) {
    const size_t attempts = test_compile_with_failure_at(fail_at, 0, &clean);
    BB_TEST_ASSERT(clean && attempts >= fail_at);
  }
  return 1;
}

const bb_test_case bb_program_tests[] = {
  {"starter program compiles lazily and renders exactly", test_starter_program_compiles_lazily_and_renders_exactly},
  {"reference module lowers analytic alpha map", test_reference_module_lowers_analytic_alpha_map_with_artifact_path},
  {"fluent image operations lower to canonical graph", test_fluent_image_operations_lower_to_canonical_graph},
  {"radial vectors, color transforms, and unary lifting", test_radial_vector_color_transforms_and_unary_collection_lifting},
  {"scalar parameter override recompiles graph shape", test_scalar_parameter_override_recompiles_graph_shape},
  {"semantic import duplicate cycle and type diagnostics", test_semantic_diagnostics_cover_imports_duplicates_cycles_and_types},
  {"collection mask uses equal-length zip semantics", test_collection_collection_mask_requires_equal_zip_lengths},
  {"host-resolved modules assets and lazy decode", test_host_resolved_modules_assets_and_lazy_decode},
  {"module and asset cycles are diagnostics", test_module_and_asset_cycles_are_diagnostics},
  {"asset dependency views survive nested resolver calls", test_asset_dependency_views_are_copied_before_nested_resolution},
  {"semantic compile allocation-failure sweep", test_semantic_compile_allocation_failure_sweep},
};

const size_t bb_program_test_count = sizeof(bb_program_tests) / sizeof(bb_program_tests[0]);
