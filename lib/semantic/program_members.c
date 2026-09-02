#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int bb_program_positive_dimension(bb_semantic_value value, uint32_t *out_dimension) {
  if (value.type != BB_SEMANTIC_INTEGER || value.data.integer <= 0 || value.data.integer > UINT32_MAX) return 0;
  *out_dimension = (uint32_t)value.data.integer;
  return 1;
}

static int bb_program_i32(bb_semantic_value value, int32_t *out_value) {
  if (value.type != BB_SEMANTIC_INTEGER || value.data.integer < INT32_MIN || value.data.integer > INT32_MAX)
    return 0;
  *out_value = (int32_t)value.data.integer;
  return 1;
}

static int bb_program_opacity_value(bb_semantic_value value, double *out_opacity) {
  double opacity;
  if (value.type == BB_SEMANTIC_PERCENTAGE) opacity = value.data.number / 100.0;
  else if (value.type == BB_SEMANTIC_NUMBER || value.type == BB_SEMANTIC_INTEGER)
    opacity = value.type == BB_SEMANTIC_INTEGER ? (double)value.data.integer : value.data.number;
  else return 0;
  if (!isfinite(opacity) || opacity < 0.0 || opacity > 1.0) return 0;
  *out_opacity = opacity;
  return 1;
}

static bb_program_callback_state *bb_program_new_transform_callback(
  bb_program *program,
  const bb_program_callback_state *configuration,
  bb_span span
) {
  bb_program_callback_state *state = bb_program_new_callback(program, configuration->kind, span);
  if (state == NULL) return NULL;
  state->width = configuration->width;
  state->height = configuration->height;
  state->x = configuration->x;
  state->y = configuration->y;
  state->turns = configuration->turns;
  state->opacity = configuration->opacity;
  memcpy(state->colors, configuration->colors, sizeof(state->colors));
  return state;
}

static bb_semantic_value bb_program_lift_image_transform(
  bb_program *program,
  bb_semantic_value receiver,
  const bb_program_callback_state *configuration,
  bb_span span,
  const char *type_error
) {
  bb_semantic_value result = bb_semantic_error();
  bb_status status;
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result = receiver;
    result.data.image.encoded_alias = 0;
    status = bb_program_apply_image_transform(configuration, receiver.data.image.node, &result.data.image.node);
    if (status == BB_STATUS_OK)
      status = bb_image_graph_node_dimensions(
        program->graph,
        result.data.image.node,
        &result.data.image.width,
        &result.data.image.height
      );
    if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, span);
  } else if (receiver.type == BB_SEMANTIC_COLLECTION &&
             receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    bb_program_callback_state *state = bb_program_new_transform_callback(program, configuration, span);
    bb_collection *plan = NULL;
    if (state == NULL) return result;
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status == BB_STATUS_OK) {
      result.type = BB_SEMANTIC_COLLECTION;
      result.data.collection.plan = plan;
      result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
    }
  } else {
    bb_program_diagnostic_span(program, BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH, type_error, span);
    return result;
  }
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    bb_semantic_release(&result);
  }
  return result;
}

static bb_semantic_value bb_program_member_map(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_semantic_value callable = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_program_callback_state *state;
  bb_collection *plan = NULL;
  bb_status status;
  if (member_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "map() expects one callable.",
      member_info.span
    );
    return result;
  }
  callable = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  if (receiver.type != BB_SEMANTIC_COLLECTION || callable.type != BB_SEMANTIC_CALLABLE ||
      !((receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_PALETTE &&
         callable.data.callable == BB_SEMANTIC_CALLABLE_FILL) ||
        (receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACT_PAIRS &&
         (callable.data.callable == BB_SEMANTIC_CALLABLE_MASK_PAIR ||
          callable.data.callable == BB_SEMANTIC_CALLABLE_OVER_PAIR)))) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "map() expects palette/fill or an artifact product with mask-pair/over-pair.",
      member_info.span
    );
    bb_semantic_release(&callable);
    return result;
  }
  state = bb_program_new_callback(
    program,
    callable.data.callable == BB_SEMANTIC_CALLABLE_FILL
      ? BB_PROGRAM_CALLBACK_FILL
      : callable.data.callable == BB_SEMANTIC_CALLABLE_MASK_PAIR
          ? BB_PROGRAM_CALLBACK_MASK_PAIR
          : BB_PROGRAM_CALLBACK_OVER_PAIR,
    member_info.span
  );
  if (state == NULL) return result;
  status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = plan;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
  bb_semantic_release(&callable);
  return result;
}

static bb_semantic_value bb_program_member_size(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value width_value = bb_semantic_error();
  bb_semantic_value height_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  uint32_t width;
  uint32_t height;
  if (member_info.child_count != 3 && member_info.child_count != 4) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "size() expects one or two integer dimensions.",
      member_info.span
    );
    return result;
  }
  width_value = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  height_value = member_info.child_count == 4
                   ? bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 3))
                   : width_value;
  if (!bb_program_positive_dimension(width_value, &width) || !bb_program_positive_dimension(height_value, &height)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "size() dimensions must be positive integers.",
      member_info.span
    );
    goto cleanup;
  }
  if (receiver.type == BB_SEMANTIC_IMAGE && receiver.data.image.width == width &&
      receiver.data.image.height == height) {
    result = receiver;
    goto cleanup;
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_SIZE;
  configuration.graph = program->graph;
  configuration.width = width;
  configuration.height = height;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "size() requires an image or image collection."
  );

cleanup:
  if (member_info.child_count == 4) bb_semantic_release(&height_value);
  bb_semantic_release(&width_value);
  return result;
}

static bb_semantic_value bb_program_member_mask(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_semantic_value mask = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_status status;
  if (member_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "mask() expects one image or image collection.",
      member_info.span
    );
    return result;
  }
  mask = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  if (receiver.type == BB_SEMANTIC_IMAGE && mask.type == BB_SEMANTIC_IMAGE) {
    result.type = BB_SEMANTIC_IMAGE;
    result.data.image.width = receiver.data.image.width;
    result.data.image.height = receiver.data.image.height;
    status = bb_image_graph_add_mask(
      program->graph,
      receiver.data.image.node,
      mask.data.image.node,
      BB_MASK_REPLACE,
      &result.data.image.node
    );
    if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
    if (status != BB_STATUS_OK) {
      bb_program_fail(program, status);
      result = bb_semantic_error();
    }
  } else if (receiver.type == BB_SEMANTIC_COLLECTION &&
             receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS && mask.type == BB_SEMANTIC_IMAGE) {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_MASK_RIGHT, member_info.span);
    bb_collection *plan = NULL;
    if (state != NULL) {
      state->image = mask.data.image.node;
      status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
      if (status != BB_STATUS_OK) bb_program_fail(program, status);
      else {
        result.type = BB_SEMANTIC_COLLECTION;
        result.data.collection.plan = plan;
        result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
      }
    }
  } else if (receiver.type == BB_SEMANTIC_IMAGE && mask.type == BB_SEMANTIC_COLLECTION &&
             mask.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_MASK_LEFT, member_info.span);
    bb_collection *plan = NULL;
    if (state != NULL) {
      state->image = receiver.data.image.node;
      status = bb_collection_map(mask.data.collection.plan, 1, bb_program_map_callback, state, &plan);
      if (status != BB_STATUS_OK) bb_program_fail(program, status);
      else {
        result.type = BB_SEMANTIC_COLLECTION;
        result.data.collection.plan = plan;
        result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
      }
    }
  } else if (receiver.type == BB_SEMANTIC_COLLECTION && mask.type == BB_SEMANTIC_COLLECTION &&
             receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS &&
             mask.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_MASK_PAIR, member_info.span);
    bb_collection *zipped = NULL;
    bb_collection *plan = NULL;
    if (state != NULL) {
      status = bb_collection_zip(receiver.data.collection.plan, mask.data.collection.plan, &zipped);
      if (status == BB_STATUS_OK) status = bb_collection_map(zipped, 1, bb_program_map_callback, state, &plan);
      bb_collection_release(zipped);
      if (status != BB_STATUS_OK) bb_program_fail(program, status);
      else {
        result.type = BB_SEMANTIC_COLLECTION;
        result.data.collection.plan = plan;
        result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
      }
    }
  } else {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "mask() requires image operands; collection/collection uses equal-length zip.",
      member_info.span
    );
  }
  bb_semantic_release(&mask);
  return result;
}

static bb_semantic_value bb_program_member_over(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_semantic_value source = bb_semantic_error();
  bb_semantic_value opacity_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  double opacity = 1.0;
  bb_status status;
  if (member_info.child_count != 3 && member_info.child_count != 4) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "over() expects an image and optional opacity.",
      member_info.span
    );
    return result;
  }
  source = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  if (member_info.child_count == 4) {
    opacity_value = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 3));
    if (!bb_program_opacity_value(opacity_value, &opacity)) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "over() opacity must be 0..1 or 0%..100%.",
        member_info.span
      );
      goto cleanup;
    }
  }
  if (receiver.type != BB_SEMANTIC_IMAGE || source.type != BB_SEMANTIC_IMAGE) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "over() requires two images.",
      member_info.span
    );
    goto cleanup;
  }
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = receiver.data.image.width;
  result.data.image.height = receiver.data.image.height;
  status = bb_image_graph_add_composite(
    program->graph,
    receiver.data.image.node,
    source.data.image.node,
    0,
    0,
    opacity,
    &result.data.image.node
  );
  if (status == BB_STATUS_OK)
    status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_semantic_release(&opacity_value);
  bb_semantic_release(&source);
  return result;
}

static bb_semantic_value bb_program_member_opacity(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  double opacity;
  if (member_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "opacity() expects one value.",
      member_info.span
    );
    return result;
  }
  value = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  if (!bb_program_opacity_value(value, &opacity)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "opacity() requires an image and a value in 0..1 or 0%..100%.",
      member_info.span
    );
    goto cleanup;
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_OPACITY;
  configuration.graph = program->graph;
  configuration.opacity = opacity;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "opacity() requires an image or image collection."
  );

cleanup:
  bb_semantic_release(&value);
  return result;
}

static bb_semantic_value bb_program_member_rotate(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value turns_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  int32_t turns;
  if (member_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "rotate() expects an integer quarter-turn count.",
      member_info.span
    );
    return result;
  }
  turns_value = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  if (!bb_program_i32(turns_value, &turns)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "rotate() requires an image and an integer quarter-turn count.",
      member_info.span
    );
    goto cleanup;
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_ROTATE;
  configuration.graph = program->graph;
  configuration.turns = turns;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "rotate() requires an image or image collection."
  );

cleanup:
  bb_semantic_release(&turns_value);
  return result;
}

static bb_semantic_value bb_program_member_crop(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value arguments[4];
  bb_semantic_value result = bb_semantic_error();
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  size_t index;
  memset(arguments, 0, sizeof(arguments));
  if (member_info.child_count != 6) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "crop() expects x, y, width, and height.",
      member_info.span
    );
    return result;
  }
  for (index = 0; index < 4; index += 1)
    arguments[index] = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, index + 2));
  if (!bb_program_i32(arguments[0], &x) ||
      !bb_program_i32(arguments[1], &y) || !bb_program_positive_dimension(arguments[2], &width) ||
      !bb_program_positive_dimension(arguments[3], &height)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "crop() requires an image, integer coordinates, and positive dimensions.",
      member_info.span
    );
    goto cleanup;
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_CROP;
  configuration.graph = program->graph;
  configuration.x = x;
  configuration.y = y;
  configuration.width = width;
  configuration.height = height;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "crop() requires an image or image collection."
  );

cleanup:
  for (index = 0; index < 4; index += 1) bb_semantic_release(&arguments[index]);
  return result;
}

static bb_semantic_value bb_program_member_canvas(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value arguments[4];
  bb_semantic_value result = bb_semantic_error();
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  size_t index;
  memset(arguments, 0, sizeof(arguments));
  if (member_info.child_count != 6) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "canvas() expects width, height, x, and y.",
      member_info.span
    );
    return result;
  }
  for (index = 0; index < 4; index += 1)
    arguments[index] = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, index + 2));
  if (!bb_program_positive_dimension(arguments[0], &width) ||
      !bb_program_positive_dimension(arguments[1], &height) || !bb_program_i32(arguments[2], &x) ||
      !bb_program_i32(arguments[3], &y)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "canvas() requires an image, positive dimensions, and integer coordinates.",
      member_info.span
    );
    goto cleanup;
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_CANVAS;
  configuration.graph = program->graph;
  configuration.width = width;
  configuration.height = height;
  configuration.x = x;
  configuration.y = y;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "canvas() requires an image or image collection."
  );

cleanup:
  for (index = 0; index < 4; index += 1) bb_semantic_release(&arguments[index]);
  return result;
}

static bb_semantic_value bb_program_member_invert_alpha(
  bb_program *program,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  if (member_info.child_count != 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "invert-alpha() expects no arguments.",
      member_info.span
    );
    return bb_semantic_error();
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_INVERT_ALPHA;
  configuration.graph = program->graph;
  return bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "invert-alpha() requires an image or image collection."
  );
}

static bb_semantic_value bb_program_member_color_transform(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver,
  bb_program_callback_kind kind
) {
  bb_program_callback_state configuration;
  bb_semantic_value color = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  if (member_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "rgb()/tint() expects one color.",
      member_info.span
    );
    return result;
  }
  color = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 2));
  if (color.type != BB_SEMANTIC_COLOR) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "rgb()/tint() requires a color argument.",
      member_info.span
    );
    goto cleanup;
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = kind;
  configuration.graph = program->graph;
  configuration.colors[0] = color.data.color;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "rgb()/tint() requires an image or image collection."
  );

cleanup:
  bb_semantic_release(&color);
  return result;
}

static bb_semantic_value bb_program_member_remap(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value colors[4];
  bb_semantic_value result = bb_semantic_error();
  size_t index;
  memset(colors, 0, sizeof(colors));
  if (member_info.child_count != 6) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "remap() expects source foreground/background and target foreground/background colors.",
      member_info.span
    );
    return result;
  }
  for (index = 0; index < 4; index += 1) {
    colors[index] = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, index + 2));
    if (colors[index].type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "remap() arguments must be colors.",
        member_info.span
      );
      goto cleanup;
    }
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_REMAP_TWO_COLOR;
  configuration.graph = program->graph;
  for (index = 0; index < 4; index += 1) configuration.colors[index] = colors[index].data.color;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "remap() requires an image or image collection."
  );

cleanup:
  for (index = 0; index < 4; index += 1) bb_semantic_release(&colors[index]);
  return result;
}

static bb_semantic_value bb_program_member_shift_rgb(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info,
  bb_semantic_value receiver
) {
  bb_program_callback_state configuration;
  bb_semantic_value colors[2];
  bb_semantic_value result = bb_semantic_error();
  size_t index;
  memset(colors, 0, sizeof(colors));
  if (member_info.child_count != 4) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "shift-rgb() expects source and target base colors.",
      member_info.span
    );
    return result;
  }
  for (index = 0; index < 2; index += 1) {
    colors[index] = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, index + 2));
    if (colors[index].type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "shift-rgb() arguments must be colors.",
        member_info.span
      );
      goto cleanup;
    }
  }
  memset(&configuration, 0, sizeof(configuration));
  configuration.kind = BB_PROGRAM_CALLBACK_SHIFT_RGB;
  configuration.graph = program->graph;
  configuration.colors[0] = colors[0].data.color;
  configuration.colors[1] = colors[1].data.color;
  result = bb_program_lift_image_transform(
    program,
    receiver,
    &configuration,
    member_info.span,
    "shift-rgb() requires an image or image collection."
  );

cleanup:
  for (index = 0; index < 2; index += 1) bb_semantic_release(&colors[index]);
  return result;
}

bb_semantic_value bb_program_evaluate_member(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info
) {
  bb_semantic_value receiver = bb_program_evaluate(program, syntax, bb_program_child(syntax, member, 0));
  const bb_string_view method = bb_program_node_text(syntax, bb_program_child(syntax, member, 1));
  bb_semantic_value result;
  if (bb_string_view_is(method, "map")) result = bb_program_member_map(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "size"))
    result = bb_program_member_size(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "mask"))
    result = bb_program_member_mask(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "over"))
    result = bb_program_member_over(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "opacity"))
    result = bb_program_member_opacity(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "rotate"))
    result = bb_program_member_rotate(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "crop"))
    result = bb_program_member_crop(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "canvas"))
    result = bb_program_member_canvas(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "invert-alpha"))
    result = bb_program_member_invert_alpha(program, member_info, receiver);
  else if (bb_string_view_is(method, "rgb"))
    result = bb_program_member_color_transform(
      program, syntax, member, member_info, receiver, BB_PROGRAM_CALLBACK_SET_VISIBLE_RGB
    );
  else if (bb_string_view_is(method, "tint"))
    result = bb_program_member_color_transform(
      program, syntax, member, member_info, receiver, BB_PROGRAM_CALLBACK_TINT_CHROMA
    );
  else if (bb_string_view_is(method, "remap"))
    result = bb_program_member_remap(program, syntax, member, member_info, receiver);
  else if (bb_string_view_is(method, "shift-rgb"))
    result = bb_program_member_shift_rgb(program, syntax, member, member_info, receiver);
  else {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_METHOD,
      "Unknown method.",
      member_info.span
    );
    result = bb_semantic_error();
  }
  bb_semantic_release(&receiver);
  return result;
}
