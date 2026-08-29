#include "test_support.h"

#include <binblock/wasm.h>

#include <stdlib.h>
#include <string.h>

static int test_wasm_handle_api_compiles_queries_renders_and_recompiles(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "size := 2\n"
    "colors := palette(red: #ff0000, blue: #0000ff)\n"
    "colors.map(fill).size(size)\n";
  bb_wasm_session session = bb_wasm_session_create();
  bb_wasm_parameter_info parameter = {0};
  bb_wasm_output_info output = {0};
  bb_wasm_artifact_info artifact = {0};
  bb_wasm_graph_node_info graph_node = {0};
  bb_wasm_render_info render = {0};
  bb_wasm_trace_info trace = {0};
  uint8_t name[16];
  uint8_t key[16];
  uint8_t *pixels;
  uint32_t generation;
  const char *size_use = strrchr(source, 's');
  parameter.struct_size = sizeof(parameter);
  output.struct_size = sizeof(output);
  artifact.struct_size = sizeof(artifact);
  graph_node.struct_size = sizeof(graph_node);
  render.struct_size = sizeof(render);
  trace.struct_size = sizeof(trace);
  BB_TEST_ASSERT(session != BB_WASM_SESSION_NONE && size_use != NULL);
  BB_TEST_ASSERT(
    bb_wasm_session_compile(session, (const uint8_t *)source, (uint32_t)(sizeof(source) - 1)) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wasm_diagnostic_count(session) == 0);
  BB_TEST_ASSERT(bb_wasm_parameter_count(session) == 1);
  BB_TEST_ASSERT(bb_wasm_parameter_get(session, 0, &parameter) == BB_STATUS_OK);
  BB_TEST_ASSERT(parameter.type == BB_SEMANTIC_INTEGER && parameter.name_length == 4);
  BB_TEST_ASSERT(bb_wasm_parameter_copy_name(session, 0, name, sizeof(name)) == BB_STATUS_OK);
  BB_TEST_ASSERT(memcmp(name, "size", 4) == 0);
  BB_TEST_ASSERT(bb_wasm_output_count(session) == 1);
  BB_TEST_ASSERT(bb_wasm_output_get(session, 0, &output) == BB_STATUS_OK);
  BB_TEST_ASSERT(output.cardinality_low == 2 && output.cardinality_high == 0);
  BB_TEST_ASSERT(bb_wasm_artifact_get(session, 0, 0, 0, &artifact) == BB_STATUS_OK);
  BB_TEST_ASSERT(artifact.key_length == 3);
  BB_TEST_ASSERT(bb_wasm_graph_node_get(session, artifact.image, &graph_node) == BB_STATUS_OK);
  BB_TEST_ASSERT(graph_node.kind == BB_IMAGE_NODE_RESIZE && graph_node.width == 2 && graph_node.height == 2);
  graph_node.struct_size = sizeof(graph_node);
  BB_TEST_ASSERT(bb_wasm_graph_node_get(session, graph_node.inputs[0], &graph_node) == BB_STATUS_OK);
  BB_TEST_ASSERT(graph_node.kind == BB_IMAGE_NODE_FILL && graph_node.options[0] == UINT32_C(0xff0000ff));
  BB_TEST_ASSERT(bb_wasm_artifact_copy_key(session, 0, 0, 0, key, sizeof(key)) == BB_STATUS_OK);
  BB_TEST_ASSERT(memcmp(key, "red", 3) == 0);
  BB_TEST_ASSERT(bb_wasm_trace_at(session, (uint32_t)(size_use - source), &trace) == BB_STATUS_OK);
  BB_TEST_ASSERT(trace.type == BB_SEMANTIC_INTEGER);
  BB_TEST_ASSERT(bb_wasm_render_output_rgba(session, 0, 0, 0, NULL, 0, &render) == BB_STATUS_OK);
  BB_TEST_ASSERT(render.width == 2 && render.height == 2 && render.rgba_length == 16);
  pixels = malloc(render.rgba_length);
  BB_TEST_ASSERT(pixels != NULL);
  BB_TEST_ASSERT(
    bb_wasm_render_output_rgba(session, 0, 0, 0, pixels, render.rgba_length, &render) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 255);
  free(pixels);
  generation = bb_wasm_session_generation(session);
  BB_TEST_ASSERT(bb_wasm_parameter_set_integer(session, 0, 4, 0) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_wasm_session_generation(session) != generation);
  render.struct_size = sizeof(render);
  BB_TEST_ASSERT(bb_wasm_render_output_rgba(session, 0, 0, 0, NULL, 0, &render) == BB_STATUS_OK);
  BB_TEST_ASSERT(render.width == 4 && render.height == 4 && render.rgba_length == 64);
  bb_wasm_session_destroy(session);
  return 1;
}

static int test_wasm_diagnostics_are_copied_without_borrowed_pointers(void) {
  static const char source[] = "missing-name\n";
  bb_wasm_session session = bb_wasm_session_create();
  bb_wasm_diagnostic_info diagnostic = {0};
  uint8_t message[128];
  diagnostic.struct_size = sizeof(diagnostic);
  BB_TEST_ASSERT(session != BB_WASM_SESSION_NONE);
  BB_TEST_ASSERT(
    bb_wasm_session_compile(session, (const uint8_t *)source, (uint32_t)(sizeof(source) - 1)) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wasm_diagnostic_count(session) >= 1);
  BB_TEST_ASSERT(bb_wasm_diagnostic_get(session, 0, &diagnostic) == BB_STATUS_OK);
  BB_TEST_ASSERT(diagnostic.message_length < sizeof(message));
  BB_TEST_ASSERT(
    bb_wasm_diagnostic_copy_message(session, 0, message, sizeof(message)) == BB_STATUS_OK
  );
  message[diagnostic.message_length] = '\0';
  BB_TEST_ASSERT(diagnostic.severity == BB_DIAGNOSTIC_ERROR && diagnostic.byte_end <= sizeof(source) - 1);
  BB_TEST_ASSERT(strstr((const char *)message, "Unknown") != NULL);
  bb_wasm_session_destroy(session);
  return 1;
}

static int test_wasm_registered_resources_supply_host_resolvers(void) {
  static const char source[] =
    "import \"binblock/basic\"\n"
    "import \"host/theme\"\n"
    "asset(\"logo\", width: 1, height: 1)\n";
  static const uint8_t module_source[] = "import \"binblock/basic\"\n";
  static const uint8_t pixel[] = {12, 34, 56, 255};
  bb_wasm_session session = bb_wasm_session_create();
  bb_wasm_render_info render = {0};
  uint8_t actual[4];
  render.struct_size = sizeof(render);
  BB_TEST_ASSERT(session != BB_WASM_SESSION_NONE);
  BB_TEST_ASSERT(
    bb_wasm_session_add_module(
      session,
      (const uint8_t *)"host/theme",
      sizeof("host/theme") - 1,
      (const uint8_t *)"module:theme:v1",
      sizeof("module:theme:v1") - 1,
      module_source,
      sizeof(module_source) - 1
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(
    bb_wasm_session_add_asset_rgba(
      session,
      (const uint8_t *)"logo",
      sizeof("logo") - 1,
      (const uint8_t *)"rgba:logo:v1",
      sizeof("rgba:logo:v1") - 1,
      1,
      1,
      pixel,
      sizeof(pixel),
      NULL,
      0
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(
    bb_wasm_session_compile(session, (const uint8_t *)source, (uint32_t)(sizeof(source) - 1)) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wasm_diagnostic_count(session) == 0);
  BB_TEST_ASSERT(
    bb_wasm_render_output_rgba(session, 0, 0, 0, actual, sizeof(actual), &render) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(render.width == 1 && render.height == 1 && memcmp(actual, pixel, sizeof(pixel)) == 0);
  bb_wasm_session_clear_resources(session);
  BB_TEST_ASSERT(bb_wasm_output_count(session) == 0);
  bb_wasm_session_destroy(session);
  return 1;
}

static int test_wasm_destroyed_handle_stays_invalid_after_slot_reuse(void) {
  static const char source[] = "import \"binblock/basic\"\nfill(#ff0000)\n";
  const bb_wasm_session stale = bb_wasm_session_create();
  bb_wasm_session replacement;
  BB_TEST_ASSERT(stale != BB_WASM_SESSION_NONE);
  bb_wasm_session_destroy(stale);
  replacement = bb_wasm_session_create();
  BB_TEST_ASSERT(replacement != BB_WASM_SESSION_NONE && replacement != stale);
  BB_TEST_ASSERT(bb_wasm_session_generation(stale) == 0);
  BB_TEST_ASSERT(
    bb_wasm_session_compile(stale, (const uint8_t *)source, (uint32_t)(sizeof(source) - 1)) ==
    BB_STATUS_INVALID_ARGUMENT
  );
  BB_TEST_ASSERT(bb_wasm_output_count(stale) == 0);
  bb_wasm_session_destroy(stale);
  BB_TEST_ASSERT(bb_wasm_session_generation(replacement) != 0);
  bb_wasm_session_destroy(replacement);
  return 1;
}

static int test_wasm_asset_metadata_is_compiled_then_hydrated_on_demand(void) {
  static const char source[] = "import \"binblock/basic\"\nasset(\"lazy\", width: 1, height: 1)\n";
  static const uint8_t pixel[] = {9, 8, 7, 6};
  static const uint8_t encoded[] = {1, 2, 3};
  bb_wasm_session session = bb_wasm_session_create();
  bb_wasm_artifact_info artifact = {0};
  bb_wasm_graph_node_info node = {0};
  bb_wasm_render_info render = {0};
  uint8_t content_id[16] = {0};
  uint8_t actual[4] = {0};
  artifact.struct_size = sizeof(artifact);
  node.struct_size = sizeof(node);
  render.struct_size = sizeof(render);
  BB_TEST_ASSERT(session != BB_WASM_SESSION_NONE);
  BB_TEST_ASSERT(
    bb_wasm_session_add_asset_metadata(
      session,
      (const uint8_t *)"lazy",
      sizeof("lazy") - 1,
      (const uint8_t *)"sha256:lazy",
      sizeof("sha256:lazy") - 1,
      1,
      1,
      1
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(
    bb_wasm_session_compile(session, (const uint8_t *)source, (uint32_t)(sizeof(source) - 1)) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(bb_wasm_diagnostic_count(session) == 0);
  BB_TEST_ASSERT(bb_wasm_artifact_get(session, 0, 0, 0, &artifact) == BB_STATUS_OK);
  BB_TEST_ASSERT(bb_wasm_graph_node_get(session, artifact.image, &node) == BB_STATUS_OK);
  BB_TEST_ASSERT(node.kind == BB_IMAGE_NODE_ASSET && node.asset_content_id_length == sizeof("sha256:lazy") - 1);
  BB_TEST_ASSERT(
    bb_wasm_graph_asset_copy_content_id(session, artifact.image, content_id, sizeof(content_id)) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(memcmp(content_id, "sha256:lazy", sizeof("sha256:lazy") - 1) == 0);
  BB_TEST_ASSERT(
    bb_wasm_render_output_rgba(session, 0, 0, 0, actual, sizeof(actual), &render) == BB_STATUS_NOT_FOUND
  );
  BB_TEST_ASSERT(
    bb_wasm_session_hydrate_asset(
      session,
      (const uint8_t *)"lazy",
      sizeof("lazy") - 1,
      pixel,
      sizeof(pixel),
      encoded,
      sizeof(encoded)
    ) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(
    bb_wasm_render_output_rgba(session, 0, 0, 0, actual, sizeof(actual), &render) == BB_STATUS_OK
  );
  BB_TEST_ASSERT(memcmp(actual, pixel, sizeof(pixel)) == 0);
  bb_wasm_session_destroy(session);
  return 1;
}

const bb_test_case bb_wasm_tests[] = {
  {"Wasm handle API compile query render and recompile", test_wasm_handle_api_compiles_queries_renders_and_recompiles},
  {"Wasm diagnostics use copy-out buffers", test_wasm_diagnostics_are_copied_without_borrowed_pointers},
  {"Wasm registered resources supply host resolvers", test_wasm_registered_resources_supply_host_resolvers},
  {"Wasm asset metadata hydrates on demand", test_wasm_asset_metadata_is_compiled_then_hydrated_on_demand},
  {"Wasm stale session handles remain invalid", test_wasm_destroyed_handle_stays_invalid_after_slot_reuse},
};

const size_t bb_wasm_test_count = sizeof(bb_wasm_tests) / sizeof(bb_wasm_tests[0]);
