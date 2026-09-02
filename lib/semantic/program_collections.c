#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct bb_program_reference_alpha_spec {
  uint32_t width;
  uint32_t height;
  uint32_t geometry;
} bb_program_reference_alpha_spec;

static bb_semantic_value bb_program_call_reference_alpha_map(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  static const bb_program_reference_alpha_spec specs[18] = {
    {64, 64, 0}, {64, 64, 1}, {64, 64, 2},
    {63, 64, 3}, {63, 63, 4}, {64, 64, 5},
    {63, 63, 6}, {64, 64, 7}, {62, 62, 8},
    {64, 64, 0}, {64, 63, 1}, {64, 64, 2},
    {63, 64, 3}, {63, 63, 4}, {64, 64, 5},
    {64, 64, 7}, {63, 63, 6}, {62, 62, 8},
  };
  static const uint8_t border_levels[] = {53, 101, 146, 179, 206, 255};
  bb_semantic_value index_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_alpha_field_desc desc;
  bb_image_node field = BB_IMAGE_NODE_NONE;
  const bb_program_reference_alpha_spec *spec;
  uint32_t index;
  bb_status status;
  if (call_info.child_count != 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "reference-alpha-map() expects one map index.",
      call_info.span
    );
    return result;
  }
  index_value = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (index_value.type != BB_SEMANTIC_INTEGER || index_value.data.integer < 0 ||
      index_value.data.integer >= 18) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "reference-alpha-map() supports analytic map indices 0 through 17; map 18 is a raster asset.",
      call_info.span
    );
    goto cleanup;
  }
  index = (uint32_t)index_value.data.integer;
  spec = &specs[index];
  memset(&desc, 0, sizeof(desc));
  desc.width = spec->width;
  desc.height = spec->height;
  desc.direction = BB_ALPHA_DIRECTION_IN;
  desc.easing = BB_EASING_REFERENCE;
  desc.color = index < 9 ? (bb_rgba8){0, 0, 0, 255} : (bb_rgba8){255, 255, 255, 255};
  if (spec->geometry == 0) {
    desc.metric = BB_ALPHA_METRIC_Y;
    desc.radius = 64;
  } else if (spec->geometry == 1) {
    desc.metric = BB_ALPHA_METRIC_Y;
    desc.center_y = 63;
    desc.radius = -64;
  } else if (spec->geometry == 2) {
    desc.metric = BB_ALPHA_METRIC_X;
    desc.radius = 64;
  } else if (spec->geometry == 3) {
    desc.metric = BB_ALPHA_METRIC_X;
    desc.center_x = 63;
    desc.radius = -64;
  } else if (spec->geometry == 4) {
    desc.metric = BB_ALPHA_METRIC_EUCLIDEAN;
    desc.center_x = 31;
    desc.center_y = 31;
    desc.radius = 32;
    desc.reference_radial_rounding = 1;
  } else if (spec->geometry == 5) {
    desc.metric = BB_ALPHA_METRIC_EUCLIDEAN;
    desc.center_x = 32;
    desc.center_y = 32;
    desc.radius = 32;
    desc.direction = BB_ALPHA_DIRECTION_OUT;
    desc.reference_radial_rounding = 1;
  } else if (spec->geometry == 6) {
    desc.metric = BB_ALPHA_METRIC_CHEBYSHEV;
    desc.center_x = 31;
    desc.center_y = 31;
    desc.radius = 32;
  } else if (spec->geometry == 7) {
    desc.metric = BB_ALPHA_METRIC_CHEBYSHEV;
    desc.center_x = 32;
    desc.center_y = 32;
    desc.radius = 32;
    desc.direction = BB_ALPHA_DIRECTION_OUT;
  } else {
    desc.metric = BB_ALPHA_METRIC_BORDER;
    desc.radius = 1;
    desc.levels = border_levels;
    desc.level_count = sizeof(border_levels);
  }
  status = bb_image_graph_add_alpha_field(program->graph, &desc, &field);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, field, call_info.span);
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = spec->width;
  result.data.image.height = spec->height;
  result.data.image.node = field;
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_semantic_release(&index_value);
  return result;
}

static bb_semantic_value bb_program_call_product(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value left = bb_semantic_error();
  bb_semantic_value right = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_collection *plan = NULL;
  bb_status status;
  if (call_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "product() expects two collections.",
      call_info.span
    );
    return result;
  }
  left = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  right = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 2));
  if (left.type != BB_SEMANTIC_COLLECTION || right.type != BB_SEMANTIC_COLLECTION ||
      left.data.collection.shape != BB_SEMANTIC_COLLECTION_ARTIFACTS ||
      right.data.collection.shape != BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "product() currently combines two artifact collections.",
      call_info.span
    );
    goto cleanup;
  }
  status = bb_collection_product(left.data.collection.plan, right.data.collection.plan, &plan);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    goto cleanup;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = plan;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACT_PAIRS;

cleanup:
  bb_semantic_release(&right);
  bb_semantic_release(&left);
  return result;
}

static bb_semantic_value bb_program_call_collect(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value result = bb_semantic_error();
  if (call_info.child_count != 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "collect() expects one collection value.",
      call_info.span
    );
    return result;
  }
  result = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (result.type != BB_SEMANTIC_COLLECTION) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "collect() expects an array or collection.",
      call_info.span
    );
    bb_semantic_release(&result);
    return bb_semantic_error();
  }
  return result;
}

static bb_semantic_value bb_program_call_union(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value result = bb_semantic_error();
  bb_collection *combined = NULL;
  size_t index;
  if (call_info.child_count < 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "union() expects one or more artifact collections.",
      call_info.span
    );
    return result;
  }
  for (index = 1; index < call_info.child_count; index += 1) {
    bb_semantic_value item = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, index));
    if (item.type != BB_SEMANTIC_COLLECTION ||
        item.data.collection.shape != BB_SEMANTIC_COLLECTION_ARTIFACTS) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "union() operands must be artifact collections.",
        call_info.span
      );
      bb_semantic_release(&item);
      bb_collection_release(combined);
      return result;
    }
    if (combined == NULL) combined = bb_collection_retain(item.data.collection.plan);
    else {
      bb_collection *next = NULL;
      bb_status status = bb_collection_concat(combined, item.data.collection.plan, &next);
      bb_collection_release(combined);
      combined = next;
      if (status != BB_STATUS_OK) {
        bb_program_fail(program, status);
        bb_semantic_release(&item);
        return result;
      }
    }
    bb_semantic_release(&item);
  }
  if (combined == NULL) {
    bb_program_fail(program, BB_STATUS_LIMIT_EXCEEDED);
    return result;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = combined;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
  return result;
}

bb_semantic_value bb_program_evaluate_call(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  const bb_syntax_node callee = bb_program_child(syntax, call, 0);
  const bb_string_view name = bb_program_node_text(syntax, callee);
  if (bb_string_view_is(name, "reference-alpha-map")) {
    if (program->has_reference_module)
      return bb_program_call_reference_alpha_map(program, syntax, call, call_info);
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_FUNCTION,
      "reference-alpha-map() requires import \"binblock/reference-set\".",
      call_info.span
    );
    return bb_semantic_error();
  }
  if (!program->has_basic_module) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_FUNCTION,
      "Standard functions require import \"binblock/basic\".",
      call_info.span
    );
    return bb_semantic_error();
  }
  if (bb_string_view_is(name, "palette")) return bb_program_call_palette(program, syntax, call, call_info);
  if (bb_string_view_is(name, "fill")) return bb_program_call_fill(program, syntax, call, call_info);
  if (bb_string_view_is(name, "artifact")) return bb_program_call_artifact(program, syntax, call, call_info);
  if (bb_string_view_is(name, "asset")) return bb_program_call_asset(program, syntax, call, call_info);
  if (bb_string_view_is(name, "product")) return bb_program_call_product(program, syntax, call, call_info);
  if (bb_string_view_is(name, "collect")) return bb_program_call_collect(program, syntax, call, call_info);
  if (bb_string_view_is(name, "union")) return bb_program_call_union(program, syntax, call, call_info);
  if (bb_string_view_is(name, "lg") || bb_string_view_is(name, "lin-grad") ||
      bb_string_view_is(name, "linear-gradient"))
    return bb_program_call_linear_gradient(program, syntax, call, call_info);
  if (bb_string_view_is(name, "rg") || bb_string_view_is(name, "rad-grad") ||
      bb_string_view_is(name, "radial-gradient"))
    return bb_program_call_radial_gradient(program, syntax, call, call_info);
  bb_program_diagnostic_span(
    program,
    BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_FUNCTION,
    "Unknown standard function.",
    call_info.span
  );
  return bb_semantic_error();
}
