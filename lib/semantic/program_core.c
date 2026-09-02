#include "program_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bb_semantic_value bb_semantic_error(void) {
  bb_semantic_value value;
  memset(&value, 0, sizeof(value));
  value.type = BB_SEMANTIC_ERROR;
  return value;
}

bb_semantic_value bb_semantic_copy(bb_semantic_value value) {
  if (value.type == BB_SEMANTIC_COLLECTION && value.data.collection.plan != NULL) {
    value.data.collection.plan = bb_collection_retain(value.data.collection.plan);
    if (value.data.collection.plan == NULL) return bb_semantic_error();
  }
  return value;
}

void bb_semantic_release(bb_semantic_value *value) {
  if (value == NULL) return;
  if (value->type == BB_SEMANTIC_COLLECTION) bb_collection_release(value->data.collection.plan);
  *value = bb_semantic_error();
}

int bb_program_value_is_parameter(const bb_semantic_value *value) {
  return value->type == BB_SEMANTIC_BOOL || value->type == BB_SEMANTIC_INTEGER ||
         value->type == BB_SEMANTIC_NUMBER || value->type == BB_SEMANTIC_DEGREES ||
         value->type == BB_SEMANTIC_PERCENTAGE || value->type == BB_SEMANTIC_COLOR;
}

bb_status bb_program_grow(
  bb_program *program,
  void **items,
  size_t *capacity,
  size_t item_size,
  size_t alignment,
  size_t required
) {
  size_t new_capacity;
  size_t old_bytes;
  size_t new_bytes;
  void *replacement;
  bb_status status;
  const size_t maximum = program->limits.max_syntax_nodes;
  if (required > maximum) return BB_STATUS_LIMIT_EXCEEDED;
  if (required <= *capacity) return BB_STATUS_OK;
  new_capacity = *capacity == 0 ? (maximum < 16 ? maximum : 16) : *capacity;
  while (new_capacity < required) {
    size_t doubled;
    if (!bb_size_multiply(new_capacity, 2, &doubled)) return BB_STATUS_OVERFLOW;
    new_capacity = doubled > maximum ? maximum : doubled;
  }
  if (!bb_size_multiply(*capacity, item_size, &old_bytes) ||
      !bb_size_multiply(new_capacity, item_size, &new_bytes)) return BB_STATUS_OVERFLOW;
  status = bb_context_reallocate(program->context, *items, old_bytes, new_bytes, alignment, &replacement);
  if (status != BB_STATUS_OK) return status;
  *items = replacement;
  *capacity = new_capacity;
  return BB_STATUS_OK;
}

void bb_program_fail(bb_program *program, bb_status status) {
  if (program->status == BB_STATUS_OK && status != BB_STATUS_OK) program->status = status;
}

void bb_program_diagnostic_span(
  bb_program *program,
  bb_semantic_diagnostic_code code,
  const char *message,
  bb_span span
) {
  bb_status status;
  if (program->status != BB_STATUS_OK) return;
  status = bb_diagnostic_store_push(
    &program->diagnostics,
    BB_DIAGNOSTIC_ERROR,
    (uint32_t)code,
    (bb_string_view){message, strlen(message)},
    span,
    NULL,
    0
  );
  bb_program_fail(program, status);
}

int bb_string_view_equal(bb_string_view left, bb_string_view right) {
  return left.length == right.length && (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}

int bb_string_view_is(bb_string_view value, const char *text) {
  const bb_string_view expected = {text, strlen(text)};
  return bb_string_view_equal(value, expected);
}

int bb_program_unquote(bb_string_view string, bb_string_view *out_value) {
  if (out_value == NULL || string.length < 2 ||
      (string.data[0] != '"' && string.data[0] != '\'') || string.data[string.length - 1] != string.data[0]) return 0;
  out_value->data = string.data + 1;
  out_value->length = string.length - 2;
  return 1;
}

bb_status bb_program_node_info(
  const bb_syntax_tree *syntax,
  bb_syntax_node node,
  bb_syntax_node_info *out_info
) {
  return bb_syntax_tree_node(syntax, node, out_info);
}

bb_syntax_node bb_program_child(const bb_syntax_tree *syntax, bb_syntax_node node, size_t index) {
  bb_syntax_node child = BB_SYNTAX_NODE_NONE;
  (void)bb_syntax_tree_child(syntax, node, index, &child);
  return child;
}

bb_string_view bb_program_token_text(const bb_syntax_tree *syntax, uint32_t token_index) {
  bb_syntax_token_info token;
  if (token_index == BB_SYNTAX_TOKEN_NONE || bb_syntax_tree_token(syntax, token_index, &token) != BB_STATUS_OK)
    return (bb_string_view){NULL, 0};
  return token.text;
}

bb_string_view bb_program_node_text(const bb_syntax_tree *syntax, bb_syntax_node node) {
  bb_syntax_node_info info;
  if (bb_program_node_info(syntax, node, &info) != BB_STATUS_OK) return (bb_string_view){NULL, 0};
  return bb_program_token_text(syntax, info.primary_token);
}

void bb_program_trace_value(
  bb_program *program,
  bb_span span,
  const bb_semantic_value *value
) {
  size_t index;
  bb_status status;
  if (program->status != BB_STATUS_OK) return;
  for (index = 0; index < program->trace_count; index += 1) {
    bb_semantic_trace *existing = &program->traces[index];
    if (existing->span.source_id == span.source_id && existing->span.byte_start == span.byte_start &&
        existing->span.byte_end == span.byte_end) {
      existing->type = value->type;
      existing->image = value->type == BB_SEMANTIC_IMAGE ? value->data.image.node : BB_IMAGE_NODE_NONE;
      return;
    }
  }
  status = bb_program_grow(
    program,
    (void **)&program->traces,
    &program->trace_capacity,
    sizeof(*program->traces),
    _Alignof(bb_semantic_trace),
    program->trace_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  program->traces[program->trace_count] = (bb_semantic_trace){
    span,
    value->type,
    value->type == BB_SEMANTIC_IMAGE ? value->data.image.node : BB_IMAGE_NODE_NONE,
    UINT64_MAX,
  };
  program->trace_count += 1;
}

static int bb_hex_digit(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

static uint8_t bb_hex_byte(const char *text) {
  return (uint8_t)((bb_hex_digit(text[0]) << 4) | bb_hex_digit(text[1]));
}

int bb_parse_color(bb_string_view text, bb_rgba8 *out_color) {
  if ((text.length != 7 && text.length != 9) || text.data == NULL || text.data[0] != '#') return 0;
  out_color->red = bb_hex_byte(text.data + 1);
  out_color->green = bb_hex_byte(text.data + 3);
  out_color->blue = bb_hex_byte(text.data + 5);
  out_color->alpha = text.length == 9 ? bb_hex_byte(text.data + 7) : 255;
  return 1;
}

int bb_parse_numeric_token(
  bb_syntax_token_info token,
  double *out_number,
  int64_t *out_integer,
  int *out_is_integer
) {
  size_t length = token.text.length;
  size_t index = 0;
  int negative = 0;
  double value = 0.0;
  double fraction_scale = 0.1;
  int exponent = 0;
  int exponent_negative = 0;
  int saw_digit = 0;
  int integer_syntax = 1;
  uint64_t integer_value = 0;
  if (token.unit == BB_SYNTAX_UNIT_DEGREES) length -= 3;
  else if (token.unit == BB_SYNTAX_UNIT_PERCENTAGE) length -= 1;
  if (index < length && token.text.data[index] == '-') {
    negative = 1;
    index += 1;
  }
  while (index < length && token.text.data[index] >= '0' && token.text.data[index] <= '9') {
    const uint32_t digit = (uint32_t)(token.text.data[index] - '0');
    saw_digit = 1;
    value = value * 10.0 + digit;
    if (integer_value <= (UINT64_MAX - digit) / 10) integer_value = integer_value * 10 + digit;
    else integer_syntax = 0;
    index += 1;
  }
  if (index < length && token.text.data[index] == '.') {
    integer_syntax = 0;
    index += 1;
    while (index < length && token.text.data[index] >= '0' && token.text.data[index] <= '9') {
      saw_digit = 1;
      value += (double)(token.text.data[index] - '0') * fraction_scale;
      fraction_scale *= 0.1;
      index += 1;
    }
  }
  if (index < length && (token.text.data[index] == 'e' || token.text.data[index] == 'E')) {
    integer_syntax = 0;
    index += 1;
    if (index < length && (token.text.data[index] == '+' || token.text.data[index] == '-')) {
      exponent_negative = token.text.data[index] == '-';
      index += 1;
    }
    while (index < length && token.text.data[index] >= '0' && token.text.data[index] <= '9') {
      if (exponent < 10000) exponent = exponent * 10 + (token.text.data[index] - '0');
      index += 1;
    }
  }
  if (!saw_digit || index != length) return 0;
  while (exponent > 0 && isfinite(value) && value != 0.0) {
    const int step = exponent > 16 ? 16 : exponent;
    static const double powers[] = {
      1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, 10000000.0, 100000000.0,
      1000000000.0, 10000000000.0, 100000000000.0, 1000000000000.0, 10000000000000.0,
      100000000000000.0, 1000000000000000.0, 10000000000000000.0,
    };
    value = exponent_negative ? value / powers[step] : value * powers[step];
    exponent -= step;
  }
  if (negative) value = -value;
  if (!isfinite(value)) return 0;
  *out_number = value;
  *out_is_integer = 0;
  if (token.unit == BB_SYNTAX_UNIT_NONE && integer_syntax) {
    const uint64_t negative_limit = (uint64_t)INT64_MAX + 1;
    if ((!negative && integer_value <= (uint64_t)INT64_MAX) || (negative && integer_value <= negative_limit)) {
      *out_integer = negative ? (integer_value == negative_limit ? INT64_MIN : -(int64_t)integer_value)
                              : (int64_t)integer_value;
      *out_is_integer = 1;
    }
  }
  return 1;
}

int bb_semantic_as_number(bb_semantic_value value, double *out_number) {
  if (out_number == NULL) return 0;
  if (value.type == BB_SEMANTIC_INTEGER) *out_number = (double)value.data.integer;
  else if (value.type == BB_SEMANTIC_NUMBER) *out_number = value.data.number;
  else return 0;
  return isfinite(*out_number);
}

bb_program_binding *bb_program_find_binding(bb_program *program, bb_string_view name) {
  size_t index;
  for (index = 0; index < program->binding_count; index += 1) {
    if (!program->bindings[index].duplicate && bb_string_view_equal(program->bindings[index].name, name))
      return &program->bindings[index];
  }
  return NULL;
}

bb_semantic_value bb_program_evaluate(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node node
);

bb_semantic_value bb_program_evaluate_binding(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_program_binding *binding,
  bb_span use_span
) {
  bb_semantic_value result;
  size_t override_index;
  if (binding->state == 2) return bb_semantic_copy(binding->value);
  if (binding->state == 1) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_BINDING_CYCLE,
      "Binding cycle detected.",
      use_span
    );
    return bb_semantic_error();
  }
  binding->state = 1;
  result = bb_program_evaluate(program, syntax, binding->expression);
  for (override_index = 0; override_index < program->options.parameter_override_count; override_index += 1) {
    const bb_parameter_override *override = &program->options.parameter_overrides[override_index];
    if (!bb_string_view_equal(binding->name, override->name)) continue;
    if (result.type != override->type || !bb_program_value_is_parameter(&result)) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "Parameter override type does not match its scalar binding.",
        binding->span
      );
      result = bb_semantic_error();
      break;
    }
    if (result.type == BB_SEMANTIC_BOOL) result.data.boolean = !!override->value.boolean;
    else if (result.type == BB_SEMANTIC_INTEGER) result.data.integer = override->value.integer;
    else if (result.type == BB_SEMANTIC_NUMBER || result.type == BB_SEMANTIC_DEGREES ||
             result.type == BB_SEMANTIC_PERCENTAGE) {
      if (!isfinite(override->value.number)) {
        bb_program_fail(program, BB_STATUS_INVALID_ARGUMENT);
        result = bb_semantic_error();
      } else result.data.number = override->value.number;
    } else if (result.type == BB_SEMANTIC_COLOR) result.data.color = override->value.color;
    break;
  }
  binding->value = result;
  binding->state = 2;
  return bb_semantic_copy(binding->value);
}

bb_semantic_value bb_program_builtin_identifier(
  bb_program *program,
  bb_string_view name,
  bb_span span
) {
  bb_semantic_value value = bb_semantic_error();
  value.type = BB_SEMANTIC_COLOR;
  if (bb_string_view_is(name, "black")) value.data.color = (bb_rgba8){0, 0, 0, 255};
  else if (bb_string_view_is(name, "white")) value.data.color = (bb_rgba8){255, 255, 255, 255};
  else if (bb_string_view_is(name, "transparent") || bb_string_view_is(name, "transparent-black"))
    value.data.color = (bb_rgba8){0, 0, 0, 0};
  else if (bb_string_view_is(name, "transparent-white")) value.data.color = (bb_rgba8){255, 255, 255, 0};
  else if (bb_string_view_is(name, "red")) value.data.color = (bb_rgba8){255, 0, 0, 255};
  else if (bb_string_view_is(name, "blue")) value.data.color = (bb_rgba8){0, 0, 255, 255};
  else if (bb_string_view_is(name, "true") || bb_string_view_is(name, "false")) {
    value.type = BB_SEMANTIC_BOOL;
    value.data.boolean = bb_string_view_is(name, "true");
  } else if ((bb_string_view_is(name, "fill") || bb_string_view_is(name, "mask-pair") ||
              bb_string_view_is(name, "over-pair")) &&
             program->has_basic_module) {
    value.type = BB_SEMANTIC_CALLABLE;
    if (bb_string_view_is(name, "fill")) value.data.callable = BB_SEMANTIC_CALLABLE_FILL;
    else if (bb_string_view_is(name, "mask-pair")) value.data.callable = BB_SEMANTIC_CALLABLE_MASK_PAIR;
    else value.data.callable = BB_SEMANTIC_CALLABLE_OVER_PAIR;
  } else {
    bb_program_diagnostic_span(program, BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_NAME, "Unknown name.", span);
    return bb_semantic_error();
  }
  return value;
}

bb_program_callback_state *bb_program_new_callback(
  bb_program *program,
  bb_program_callback_kind kind,
  bb_span span
) {
  bb_program_callback_state *state = NULL;
  bb_status status = bb_context_allocate(
    program->context,
    sizeof(*state),
    _Alignof(bb_program_callback_state),
    (void **)&state
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return NULL;
  }
  memset(state, 0, sizeof(*state));
  state->next = program->callbacks;
  state->kind = kind;
  state->graph = program->graph;
  state->span = span;
  state->mask_mode = BB_MASK_REPLACE;
  program->callbacks = state;
  return state;
}

bb_status bb_program_apply_image_transform(
  const bb_program_callback_state *state,
  bb_image_node source,
  bb_image_node *out_node
) {
  if (state == NULL || source == BB_IMAGE_NODE_NONE || out_node == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  switch (state->kind) {
    case BB_PROGRAM_CALLBACK_SIZE:
      return bb_image_graph_add_resize(state->graph, source, state->width, state->height, out_node);
    case BB_PROGRAM_CALLBACK_OPACITY:
      return bb_image_graph_add_opacity(state->graph, source, state->opacity, out_node);
    case BB_PROGRAM_CALLBACK_ROTATE:
      return bb_image_graph_add_rotate(state->graph, source, state->turns, out_node);
    case BB_PROGRAM_CALLBACK_CROP:
      return bb_image_graph_add_crop(state->graph, source, state->x, state->y, state->width, state->height, out_node);
    case BB_PROGRAM_CALLBACK_CANVAS:
      return bb_image_graph_add_canvas(state->graph, source, state->width, state->height, state->x, state->y, out_node);
    case BB_PROGRAM_CALLBACK_INVERT_ALPHA:
      return bb_image_graph_add_invert_alpha(state->graph, source, out_node);
    case BB_PROGRAM_CALLBACK_SET_VISIBLE_RGB:
      return bb_image_graph_add_set_visible_rgb(state->graph, source, state->colors[0], out_node);
    case BB_PROGRAM_CALLBACK_TINT_CHROMA:
      return bb_image_graph_add_tint_chroma(state->graph, source, state->colors[0], out_node);
    case BB_PROGRAM_CALLBACK_REMAP_TWO_COLOR:
      return bb_image_graph_add_remap_two_color(
        state->graph,
        source,
        state->colors[0],
        state->colors[1],
        state->colors[2],
        state->colors[3],
        out_node
      );
    case BB_PROGRAM_CALLBACK_SHIFT_RGB:
      return bb_image_graph_add_shift_rgb(state->graph, source, state->colors[0], state->colors[1], out_node);
    default:
      return BB_STATUS_INVALID_ARGUMENT;
  }
}

bb_status bb_program_map_callback(
  void *user,
  const bb_value *input,
  size_t input_count,
  bb_value *output,
  size_t output_count
) {
  bb_program_callback_state *state = user;
  bb_artifact_value artifact;
  bb_image_node node = BB_IMAGE_NODE_NONE;
  bb_status status;
  if (state == NULL || output_count != 1) return BB_STATUS_INVALID_ARGUMENT;
  memset(&artifact, 0, sizeof(artifact));
  if (state->kind == BB_PROGRAM_CALLBACK_FILL) {
    if (input_count != 2 || input[0].kind != BB_VALUE_STRING || input[1].kind != BB_VALUE_COLOR)
      return BB_STATUS_INVALID_ARGUMENT;
    status = bb_image_graph_add_fill(state->graph, 1, 1, input[1].data.color, &node);
    artifact.key = input[0].data.string;
    artifact.path = input[0].data.string;
  } else if (state->kind >= BB_PROGRAM_CALLBACK_SIZE && state->kind <= BB_PROGRAM_CALLBACK_SHIFT_RGB) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_program_apply_image_transform(state, artifact.image, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_MASK_RIGHT) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_mask(state->graph, artifact.image, state->image, state->mask_mode, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_MASK_LEFT) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_mask(state->graph, state->image, artifact.image, state->mask_mode, &node);
  } else {
    int key_length;
    if (input_count != 2 || input[0].kind != BB_VALUE_ARTIFACT || input[1].kind != BB_VALUE_ARTIFACT ||
        input[0].data.artifact.image == BB_IMAGE_NODE_NONE || input[1].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    key_length = snprintf(
      state->key_buffer,
      sizeof(state->key_buffer),
      "%.*s--%.*s",
      (int)input[0].data.artifact.key.length,
      input[0].data.artifact.key.data,
      (int)input[1].data.artifact.key.length,
      input[1].data.artifact.key.data
    );
    if (key_length < 0 || (size_t)key_length >= sizeof(state->key_buffer)) return BB_STATUS_LIMIT_EXCEEDED;
    if (snprintf(state->path_buffer, sizeof(state->path_buffer), "%s.png", state->key_buffer) < 0 ||
        strlen(state->path_buffer) + 1 >= sizeof(state->path_buffer)) return BB_STATUS_LIMIT_EXCEEDED;
    artifact.key = (bb_string_view){state->key_buffer, (size_t)key_length};
    artifact.path = (bb_string_view){state->path_buffer, strlen(state->path_buffer)};
    if (state->kind == BB_PROGRAM_CALLBACK_OVER_PAIR)
      status = bb_image_graph_add_composite(
        state->graph,
        input[0].data.artifact.image,
        input[1].data.artifact.image,
        0,
        0,
        1.0,
        &node
      );
    else
      status = bb_image_graph_add_mask(
        state->graph,
        input[0].data.artifact.image,
        input[1].data.artifact.image,
        state->mask_mode,
        &node
      );
  }
  if (status != BB_STATUS_OK) return status;
  status = bb_image_graph_attach_span(state->graph, node, state->span);
  if (status != BB_STATUS_OK) return status;
  artifact.image = node;
  if (state->kind != BB_PROGRAM_CALLBACK_FILL) {
    artifact.alias_identity = BB_ALIAS_NONE;
    artifact.alias_target = (bb_string_view){NULL, 0};
  }
  artifact.provenance = state->span;
  memset(output, 0, sizeof(*output));
  output[0].kind = BB_VALUE_ARTIFACT;
  output[0].data.artifact = artifact;
  return BB_STATUS_OK;
}
