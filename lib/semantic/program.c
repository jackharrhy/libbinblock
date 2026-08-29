#include <binblock/program.h>
#include <binblock/module.h>

#include "checked_math.h"
#include "context_internal.h"
#include "diagnostic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef enum bb_semantic_collection_shape {
  BB_SEMANTIC_COLLECTION_NONE = 0,
  BB_SEMANTIC_COLLECTION_PALETTE = 1,
  BB_SEMANTIC_COLLECTION_ARTIFACTS = 2,
  BB_SEMANTIC_COLLECTION_ARTIFACT_PAIRS = 3
} bb_semantic_collection_shape;

typedef enum bb_semantic_callable {
  BB_SEMANTIC_CALLABLE_NONE = 0,
  BB_SEMANTIC_CALLABLE_FILL = 1,
  BB_SEMANTIC_CALLABLE_MASK_PAIR = 2
} bb_semantic_callable;

typedef struct bb_semantic_value {
  bb_semantic_type type;
  union {
    uint32_t boolean;
    int64_t integer;
    double number;
    bb_string_view string;
    bb_rgba8 color;
    struct { double x; double y; } vector2;
    struct {
      bb_image_node node;
      uint32_t width;
      uint32_t height;
      uint32_t encoded_alias;
      bb_string_view key;
      bb_string_view path;
    } image;
    struct {
      bb_collection *plan;
      bb_semantic_collection_shape shape;
    } collection;
    bb_semantic_callable callable;
  } data;
} bb_semantic_value;

typedef struct bb_program_binding {
  bb_string_view name;
  bb_span span;
  bb_syntax_node expression;
  uint32_t state;
  uint32_t duplicate;
  bb_semantic_value value;
} bb_program_binding;

typedef struct bb_program_module_record {
  char *identity;
  size_t identity_bytes;
  uint32_t state;
} bb_program_module_record;

typedef enum bb_program_callback_kind {
  BB_PROGRAM_CALLBACK_FILL,
  BB_PROGRAM_CALLBACK_SIZE,
  BB_PROGRAM_CALLBACK_OPACITY,
  BB_PROGRAM_CALLBACK_ROTATE,
  BB_PROGRAM_CALLBACK_CROP,
  BB_PROGRAM_CALLBACK_CANVAS,
  BB_PROGRAM_CALLBACK_INVERT_ALPHA,
  BB_PROGRAM_CALLBACK_SET_VISIBLE_RGB,
  BB_PROGRAM_CALLBACK_TINT_CHROMA,
  BB_PROGRAM_CALLBACK_REMAP_TWO_COLOR,
  BB_PROGRAM_CALLBACK_SHIFT_RGB,
  BB_PROGRAM_CALLBACK_MASK_RIGHT,
  BB_PROGRAM_CALLBACK_MASK_LEFT,
  BB_PROGRAM_CALLBACK_MASK_PAIR
} bb_program_callback_kind;

typedef struct bb_program_callback_state {
  struct bb_program_callback_state *next;
  bb_program_callback_kind kind;
  bb_image_graph *graph;
  bb_span span;
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  int32_t turns;
  double opacity;
  bb_rgba8 colors[4];
  bb_image_node image;
  bb_mask_mode mask_mode;
  char key_buffer[512];
  char path_buffer[512];
} bb_program_callback_state;

typedef struct bb_program_output_record {
  bb_program_output_info info;
  bb_collection *artifacts;
} bb_program_output_record;

struct bb_program {
  bb_context *context;
  bb_limits limits;
  bb_compile_options options;
  bb_status status;
  uint32_t has_basic_module;
  uint32_t has_reference_module;
  bb_image_graph *graph;
  bb_diagnostic_store diagnostics;
  size_t *diagnostic_order;
  size_t diagnostic_order_bytes;
  bb_program_binding *bindings;
  size_t binding_count;
  size_t binding_capacity;
  bb_program_module_record *modules;
  size_t module_count;
  size_t module_capacity;
  bb_semantic_trace *traces;
  size_t trace_count;
  size_t trace_capacity;
  bb_program_output_record *outputs;
  size_t output_count;
  size_t output_capacity;
  bb_program_callback_state *callbacks;
};

static bb_semantic_value bb_semantic_error(void) {
  bb_semantic_value value;
  memset(&value, 0, sizeof(value));
  value.type = BB_SEMANTIC_ERROR;
  return value;
}

static bb_semantic_value bb_semantic_copy(bb_semantic_value value) {
  if (value.type == BB_SEMANTIC_COLLECTION && value.data.collection.plan != NULL) {
    value.data.collection.plan = bb_collection_retain(value.data.collection.plan);
    if (value.data.collection.plan == NULL) return bb_semantic_error();
  }
  return value;
}

static void bb_semantic_release(bb_semantic_value *value) {
  if (value == NULL) return;
  if (value->type == BB_SEMANTIC_COLLECTION) bb_collection_release(value->data.collection.plan);
  *value = bb_semantic_error();
}

static int bb_program_value_is_parameter(const bb_semantic_value *value) {
  return value->type == BB_SEMANTIC_BOOL || value->type == BB_SEMANTIC_INTEGER ||
         value->type == BB_SEMANTIC_NUMBER || value->type == BB_SEMANTIC_DEGREES ||
         value->type == BB_SEMANTIC_PERCENTAGE || value->type == BB_SEMANTIC_COLOR;
}

static bb_status bb_program_grow(
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

static void bb_program_fail(bb_program *program, bb_status status) {
  if (program->status == BB_STATUS_OK && status != BB_STATUS_OK) program->status = status;
}

static void bb_program_diagnostic_span(
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

static int bb_string_view_equal(bb_string_view left, bb_string_view right) {
  return left.length == right.length && (left.length == 0 || memcmp(left.data, right.data, left.length) == 0);
}

static int bb_string_view_is(bb_string_view value, const char *text) {
  const bb_string_view expected = {text, strlen(text)};
  return bb_string_view_equal(value, expected);
}

static int bb_program_unquote(bb_string_view string, bb_string_view *out_value) {
  if (out_value == NULL || string.length < 2 ||
      (string.data[0] != '"' && string.data[0] != '\'') || string.data[string.length - 1] != string.data[0]) return 0;
  out_value->data = string.data + 1;
  out_value->length = string.length - 2;
  return 1;
}

static bb_status bb_program_node_info(
  const bb_syntax_tree *syntax,
  bb_syntax_node node,
  bb_syntax_node_info *out_info
) {
  return bb_syntax_tree_node(syntax, node, out_info);
}

static bb_syntax_node bb_program_child(const bb_syntax_tree *syntax, bb_syntax_node node, size_t index) {
  bb_syntax_node child = BB_SYNTAX_NODE_NONE;
  (void)bb_syntax_tree_child(syntax, node, index, &child);
  return child;
}

static bb_string_view bb_program_token_text(const bb_syntax_tree *syntax, uint32_t token_index) {
  bb_syntax_token_info token;
  if (token_index == BB_SYNTAX_TOKEN_NONE || bb_syntax_tree_token(syntax, token_index, &token) != BB_STATUS_OK)
    return (bb_string_view){NULL, 0};
  return token.text;
}

static bb_string_view bb_program_node_text(const bb_syntax_tree *syntax, bb_syntax_node node) {
  bb_syntax_node_info info;
  if (bb_program_node_info(syntax, node, &info) != BB_STATUS_OK) return (bb_string_view){NULL, 0};
  return bb_program_token_text(syntax, info.primary_token);
}

static void bb_program_trace_value(
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

static int bb_parse_color(bb_string_view text, bb_rgba8 *out_color) {
  if ((text.length != 7 && text.length != 9) || text.data == NULL || text.data[0] != '#') return 0;
  out_color->red = bb_hex_byte(text.data + 1);
  out_color->green = bb_hex_byte(text.data + 3);
  out_color->blue = bb_hex_byte(text.data + 5);
  out_color->alpha = text.length == 9 ? bb_hex_byte(text.data + 7) : 255;
  return 1;
}

static int bb_parse_numeric_token(
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

static int bb_semantic_as_number(bb_semantic_value value, double *out_number) {
  if (out_number == NULL) return 0;
  if (value.type == BB_SEMANTIC_INTEGER) *out_number = (double)value.data.integer;
  else if (value.type == BB_SEMANTIC_NUMBER) *out_number = value.data.number;
  else return 0;
  return isfinite(*out_number);
}

static bb_program_binding *bb_program_find_binding(bb_program *program, bb_string_view name) {
  size_t index;
  for (index = 0; index < program->binding_count; index += 1) {
    if (!program->bindings[index].duplicate && bb_string_view_equal(program->bindings[index].name, name))
      return &program->bindings[index];
  }
  return NULL;
}

static bb_semantic_value bb_program_evaluate(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node node
);

static bb_semantic_value bb_program_evaluate_binding(
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

static bb_semantic_value bb_program_builtin_identifier(
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
  } else if ((bb_string_view_is(name, "fill") || bb_string_view_is(name, "mask-pair")) &&
             program->has_basic_module) {
    value.type = BB_SEMANTIC_CALLABLE;
    value.data.callable = bb_string_view_is(name, "fill") ? BB_SEMANTIC_CALLABLE_FILL
                                                           : BB_SEMANTIC_CALLABLE_MASK_PAIR;
  } else {
    bb_program_diagnostic_span(program, BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_NAME, "Unknown name.", span);
    return bb_semantic_error();
  }
  return value;
}

static bb_program_callback_state *bb_program_new_callback(
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

static bb_status bb_program_map_callback(
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
  } else if (state->kind == BB_PROGRAM_CALLBACK_SIZE) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_resize(state->graph, artifact.image, state->width, state->height, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_OPACITY) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_opacity(state->graph, artifact.image, state->opacity, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_ROTATE) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_rotate(state->graph, artifact.image, state->turns, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_CROP) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_crop(state->graph, artifact.image, state->x, state->y, state->width, state->height, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_CANVAS) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_canvas(state->graph, artifact.image, state->width, state->height, state->x, state->y, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_INVERT_ALPHA) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_invert_alpha(state->graph, artifact.image, &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_SET_VISIBLE_RGB) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_set_visible_rgb(state->graph, artifact.image, state->colors[0], &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_TINT_CHROMA) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_tint_chroma(state->graph, artifact.image, state->colors[0], &node);
  } else if (state->kind == BB_PROGRAM_CALLBACK_REMAP_TWO_COLOR) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_remap_two_color(
      state->graph,
      artifact.image,
      state->colors[0],
      state->colors[1],
      state->colors[2],
      state->colors[3],
      &node
    );
  } else if (state->kind == BB_PROGRAM_CALLBACK_SHIFT_RGB) {
    if (input_count != 1 || input[0].kind != BB_VALUE_ARTIFACT || input[0].data.artifact.image == BB_IMAGE_NODE_NONE)
      return BB_STATUS_INVALID_ARGUMENT;
    artifact = input[0].data.artifact;
    status = bb_image_graph_add_shift_rgb(state->graph, artifact.image, state->colors[0], state->colors[1], &node);
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

static bb_semantic_value bb_program_call_palette(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_value *rows = NULL;
  bb_collection *collection = NULL;
  bb_semantic_value result = bb_semantic_error();
  const size_t entry_count = call_info.child_count > 0 ? call_info.child_count - 1 : 0;
  size_t value_count;
  size_t bytes;
  size_t index;
  bb_status status;
  if (entry_count == 0 || !bb_size_multiply(entry_count, 2, &value_count) ||
      !bb_size_multiply(value_count, sizeof(*rows), &bytes)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "palette() expects one or more named color arguments.",
      call_info.span
    );
    return result;
  }
  status = bb_context_allocate(program->context, bytes, _Alignof(bb_value), (void **)&rows);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  memset(rows, 0, bytes);
  for (index = 0; index < entry_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index + 1);
    bb_syntax_node_info argument_info;
    bb_semantic_value color;
    if (bb_program_node_info(syntax, argument, &argument_info) != BB_STATUS_OK ||
        argument_info.kind != BB_SYNTAX_NAMED_ARGUMENT) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "palette() entries must be named colors.",
        argument_info.span
      );
      goto cleanup;
    }
    color = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
    if (color.type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "palette() entry values must be colors.",
        argument_info.span
      );
      bb_semantic_release(&color);
      goto cleanup;
    }
    rows[index * 2].kind = BB_VALUE_STRING;
    rows[index * 2].data.string = bb_program_node_text(syntax, bb_program_child(syntax, argument, 0));
    rows[index * 2 + 1].kind = BB_VALUE_COLOR;
    rows[index * 2 + 1].data.color = color.data.color;
    bb_semantic_release(&color);
  }
  status = bb_collection_from_rows(program->context, rows, entry_count, 2, &collection);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    goto cleanup;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = collection;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_PALETTE;

cleanup:
  bb_context_deallocate(program->context, rows, bytes, _Alignof(bb_value));
  return result;
}

static bb_semantic_value bb_program_call_linear_gradient(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  const size_t argument_count = call_info.child_count > 0 ? call_info.child_count - 1 : 0;
  bb_gradient_stop *stops = NULL;
  size_t stop_count;
  size_t stop_bytes;
  bb_semantic_value angle = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_linear_gradient_desc desc;
  size_t index;
  bb_status status;
  if (argument_count < 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "lg() expects an angle and at least two color stops.",
      call_info.span
    );
    return result;
  }
  angle = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (angle.type != BB_SEMANTIC_DEGREES) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "Linear gradient angles must use degrees.",
      call_info.span
    );
    bb_semantic_release(&angle);
    return result;
  }
  stop_count = argument_count - 1;
  if (!bb_size_multiply(stop_count, sizeof(*stops), &stop_bytes)) {
    bb_program_fail(program, BB_STATUS_OVERFLOW);
    return result;
  }
  status = bb_context_allocate(program->context, stop_bytes, _Alignof(bb_gradient_stop), (void **)&stops);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  memset(stops, 0, stop_bytes);
  for (index = 0; index < stop_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index + 2);
    bb_syntax_node_info argument_info;
    bb_syntax_node color_node = argument;
    bb_semantic_value color;
    stops[index].offset = NAN;
    (void)bb_program_node_info(syntax, argument, &argument_info);
    if (argument_info.kind == BB_SYNTAX_GRADIENT_STOP) {
      bb_semantic_value offset;
      color_node = bb_program_child(syntax, argument, 0);
      offset = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
      if (offset.type != BB_SEMANTIC_PERCENTAGE || offset.data.number < 0.0 || offset.data.number > 100.0) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Gradient stop offsets must be between 0% and 100%.",
          argument_info.span
        );
        bb_semantic_release(&offset);
        goto cleanup;
      }
      stops[index].offset = offset.data.number / 100.0;
      bb_semantic_release(&offset);
    }
    color = bb_program_evaluate(program, syntax, color_node);
    if (color.type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "Gradient stops must be colors.",
        argument_info.span
      );
      bb_semantic_release(&color);
      goto cleanup;
    }
    stops[index].color = color.data.color;
    stops[index].easing = BB_EASING_LINEAR;
    bb_semantic_release(&color);
  }
  if (isnan(stops[0].offset)) stops[0].offset = 0.0;
  if (isnan(stops[stop_count - 1].offset)) stops[stop_count - 1].offset = 1.0;
  index = 1;
  while (index + 1 < stop_count) {
    size_t end;
    size_t fill;
    if (!isnan(stops[index].offset)) {
      index += 1;
      continue;
    }
    end = index + 1;
    while (end < stop_count && isnan(stops[end].offset)) end += 1;
    for (fill = index; fill < end; fill += 1) {
      const double fraction = (double)(fill - index + 1) / (double)(end - index + 1);
      stops[fill].offset = stops[index - 1].offset + (stops[end].offset - stops[index - 1].offset) * fraction;
    }
    index = end + 1;
  }
  memset(&desc, 0, sizeof(desc));
  desc.width = 64;
  desc.height = 64;
  desc.angle_degrees = angle.data.number;
  desc.stops = stops;
  desc.stop_count = stop_count;
  desc.easing = BB_EASING_LINEAR;
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = 64;
  result.data.image.height = 64;
  status = bb_image_graph_add_linear_gradient(program->graph, &desc, &result.data.image.node);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_context_deallocate(program->context, stops, stop_bytes, _Alignof(bb_gradient_stop));
  bb_semantic_release(&angle);
  return result;
}

static bb_semantic_value bb_program_call_radial_gradient(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  const size_t argument_count = call_info.child_count > 0 ? call_info.child_count - 1 : 0;
  bb_semantic_value inner = bb_semantic_error();
  bb_semantic_value outer = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_gradient_stop stops[2];
  bb_elliptical_gradient_desc desc;
  size_t index;
  size_t stop_index;
  bb_status status;
  if (argument_count < 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "rg() expects two colors followed by optional named geometry arguments.",
      call_info.span
    );
    return result;
  }
  memset(&desc, 0, sizeof(desc));
  memset(stops, 0, sizeof(stops));
  stops[0].offset = 0.0;
  stops[1].offset = 1.0;
  for (stop_index = 0; stop_index < 2; stop_index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, stop_index + 1);
    bb_syntax_node color_node = argument;
    bb_syntax_node_info argument_info;
    bb_semantic_value *color = stop_index == 0 ? &inner : &outer;
    (void)bb_program_node_info(syntax, argument, &argument_info);
    if (argument_info.kind == BB_SYNTAX_GRADIENT_STOP) {
      bb_semantic_value offset;
      color_node = bb_program_child(syntax, argument, 0);
      offset = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
      if (offset.type != BB_SEMANTIC_PERCENTAGE || offset.data.number < 0.0 || offset.data.number > 100.0) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Radial gradient stop offsets must be between 0% and 100%.",
          argument_info.span
        );
        bb_semantic_release(&offset);
        goto cleanup;
      }
      stops[stop_index].offset = offset.data.number / 100.0;
      bb_semantic_release(&offset);
    }
    *color = bb_program_evaluate(program, syntax, color_node);
    if (color->type != BB_SEMANTIC_COLOR) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "rg() inner and outer stops must be colors.",
        argument_info.span
      );
      goto cleanup;
    }
    stops[stop_index].color = color->data.color;
  }
  desc.width = 64;
  desc.height = 64;
  desc.center_x = 31.5;
  desc.center_y = 31.5;
  desc.radius_x = 32.0;
  desc.radius_y = 32.0;
  desc.easing = BB_EASING_LINEAR;
  desc.stops = stops;
  desc.stop_count = 2;
  for (index = 2; index < argument_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index + 1);
    bb_syntax_node_info argument_info;
    bb_string_view name;
    bb_semantic_value value;
    double number;
    if (bb_program_node_info(syntax, argument, &argument_info) != BB_STATUS_OK ||
        argument_info.kind != BB_SYNTAX_NAMED_ARGUMENT) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "rg() geometry arguments must be named.",
        argument_info.span
      );
      goto cleanup;
    }
    name = bb_program_node_text(syntax, bb_program_child(syntax, argument, 0));
    value = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
    if (bb_string_view_is(name, "center") && value.type == BB_SEMANTIC_VECTOR2) {
      desc.center_x = value.data.vector2.x;
      desc.center_y = value.data.vector2.y;
    } else if (bb_string_view_is(name, "radius") && bb_semantic_as_number(value, &number) && number > 0.0) {
      desc.radius_x = number;
      desc.radius_y = number;
    } else if (bb_string_view_is(name, "radius-x") && bb_semantic_as_number(value, &number) && number > 0.0)
      desc.radius_x = number;
    else if (bb_string_view_is(name, "radius-y") && bb_semantic_as_number(value, &number) && number > 0.0)
      desc.radius_y = number;
    else if (bb_string_view_is(name, "rotation") && value.type == BB_SEMANTIC_DEGREES)
      desc.rotation_radians = value.data.number * 0.017453292519943295769;
    else if (bb_string_view_is(name, "width") && value.type == BB_SEMANTIC_INTEGER &&
             value.data.integer > 0 && value.data.integer <= UINT32_MAX)
      desc.width = (uint32_t)value.data.integer;
    else if (bb_string_view_is(name, "height") && value.type == BB_SEMANTIC_INTEGER &&
             value.data.integer > 0 && value.data.integer <= UINT32_MAX)
      desc.height = (uint32_t)value.data.integer;
    else if (bb_string_view_is(name, "legacy-rounding") && value.type == BB_SEMANTIC_BOOL)
      desc.legacy_radial_rounding = !!value.data.boolean;
    else if (bb_string_view_is(name, "easing") && value.type == BB_SEMANTIC_STRING) {
      bb_string_view easing;
      if (!bb_program_unquote(value.data.string, &easing)) {
        bb_semantic_release(&value);
        goto invalid_argument;
      }
      if (bb_string_view_is(easing, "linear")) desc.easing = BB_EASING_LINEAR;
      else if (bb_string_view_is(easing, "smoothstep")) desc.easing = BB_EASING_SMOOTHSTEP;
      else if (bb_string_view_is(easing, "legacy")) desc.easing = BB_EASING_LEGACY;
      else {
        bb_semantic_release(&value);
        goto invalid_argument;
      }
    } else {
      bb_semantic_release(&value);
      goto invalid_argument;
    }
    bb_semantic_release(&value);
    continue;

invalid_argument:
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "Invalid rg() named geometry argument.",
      argument_info.span
    );
    goto cleanup;
  }
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = desc.width;
  result.data.image.height = desc.height;
  status = bb_image_graph_add_elliptical_gradient(program->graph, &desc, &result.data.image.node);
  if (status == BB_STATUS_OK)
    status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_semantic_release(&outer);
  bb_semantic_release(&inner);
  return result;
}

typedef struct bb_program_asset_stack {
  const struct bb_program_asset_stack *parent;
  bb_string_view logical_id;
} bb_program_asset_stack;

typedef struct bb_program_owned_asset {
  char *content_id;
  size_t content_id_bytes;
  uint32_t width;
  uint32_t height;
  uint32_t has_encoded_bytes;
} bb_program_owned_asset;

static void bb_program_owned_asset_destroy(bb_program *program, bb_program_owned_asset *asset) {
  if (asset == NULL) return;
  bb_context_deallocate(
    program->context,
    asset->content_id,
    asset->content_id_bytes,
    _Alignof(char)
  );
  memset(asset, 0, sizeof(*asset));
}

static int bb_program_asset_stack_contains(
  const bb_program_asset_stack *stack,
  bb_string_view logical_id
) {
  while (stack != NULL) {
    if (bb_string_view_equal(stack->logical_id, logical_id)) return 1;
    stack = stack->parent;
  }
  return 0;
}

static int bb_program_resolve_asset_recursive(
  bb_program *program,
  const bb_asset_request *request,
  bb_span span,
  const bb_program_asset_stack *parent,
  uint32_t depth,
  bb_program_owned_asset *out_asset
) {
  bb_resolved_asset resolved;
  bb_program_asset_stack stack;
  bb_string_view *dependency_copies = NULL;
  size_t dependency_bytes = 0;
  bb_status status;
  size_t index;
  memset(out_asset, 0, sizeof(*out_asset));
  if (program->options.resolve_asset == NULL) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION,
      "No host asset resolver is configured.",
      span
    );
    return 0;
  }
  if (depth > program->limits.max_import_depth) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_CYCLE,
      "Asset dependency depth limit exceeded.",
      span
    );
    return 0;
  }
  if (bb_program_asset_stack_contains(parent, request->logical_id)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_CYCLE,
      "Asset dependency cycle detected.",
      span
    );
    return 0;
  }
  memset(&resolved, 0, sizeof(resolved));
  status = program->options.resolve_asset(program->options.user, request, &resolved);
  if (status != BB_STATUS_OK || resolved.content_id.length == 0 || resolved.content_id.data == NULL ||
      resolved.width == 0 || resolved.height == 0 ||
      (resolved.dependency_count != 0 && resolved.dependencies == NULL)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION,
      "The host could not resolve this logical asset.",
      span
    );
    return 0;
  }
  if ((request->expected_content_id.length != 0 &&
       !bb_string_view_equal(request->expected_content_id, resolved.content_id)) ||
      (request->has_expected_dimensions &&
       (request->expected_width != resolved.width || request->expected_height != resolved.height))) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ASSET_CONSTRAINT,
      "Resolved asset does not satisfy its declared content or dimension constraints.",
      span
    );
    return 0;
  }
  out_asset->content_id_bytes = resolved.content_id.length;
  out_asset->width = resolved.width;
  out_asset->height = resolved.height;
  out_asset->has_encoded_bytes = !!resolved.has_encoded_bytes;
  status = bb_context_allocate(
    program->context,
    out_asset->content_id_bytes,
    _Alignof(char),
    (void **)&out_asset->content_id
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return 0;
  }
  memcpy(out_asset->content_id, resolved.content_id.data, out_asset->content_id_bytes);
  if (resolved.dependency_count != 0) {
    if (!bb_size_multiply(resolved.dependency_count, sizeof(*dependency_copies), &dependency_bytes)) {
      bb_program_fail(program, BB_STATUS_OVERFLOW);
      bb_program_owned_asset_destroy(program, out_asset);
      return 0;
    }
    status = bb_context_allocate(
      program->context,
      dependency_bytes,
      _Alignof(bb_string_view),
      (void **)&dependency_copies
    );
    if (status != BB_STATUS_OK) {
      bb_program_fail(program, status);
      bb_program_owned_asset_destroy(program, out_asset);
      return 0;
    }
    memset(dependency_copies, 0, dependency_bytes);
    for (index = 0; index < resolved.dependency_count; index += 1) {
      const bb_string_view logical = resolved.dependencies[index];
      char *logical_copy = NULL;
      if (logical.length == 0 || logical.data == NULL) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION,
          "Asset dependency has an invalid logical identifier.",
          span
        );
        break;
      }
      status = bb_context_allocate(
        program->context,
        logical.length,
        _Alignof(char),
        (void **)&logical_copy
      );
      if (status != BB_STATUS_OK) {
        bb_program_fail(program, status);
        break;
      }
      memcpy(logical_copy, logical.data, logical.length);
      dependency_copies[index] = (bb_string_view){logical_copy, logical.length};
    }
    if (index != resolved.dependency_count) {
      size_t cleanup_index;
      for (cleanup_index = 0; cleanup_index < index; cleanup_index += 1)
        bb_context_deallocate(
          program->context,
          (void *)dependency_copies[cleanup_index].data,
          dependency_copies[cleanup_index].length,
          _Alignof(char)
        );
      bb_context_deallocate(program->context, dependency_copies, dependency_bytes, _Alignof(bb_string_view));
      bb_program_owned_asset_destroy(program, out_asset);
      return 0;
    }
  }
  stack.parent = parent;
  stack.logical_id = request->logical_id;
  for (index = 0; index < resolved.dependency_count; index += 1) {
    bb_asset_request dependency_request;
    bb_program_owned_asset dependency;
    memset(&dependency_request, 0, sizeof(dependency_request));
    dependency_request.logical_id = dependency_copies[index];
    if (!bb_program_resolve_asset_recursive(
          program,
          &dependency_request,
          span,
          &stack,
          depth + 1,
          &dependency
        )) {
      break;
    }
    bb_program_owned_asset_destroy(program, &dependency);
  }
  for (size_t cleanup_index = 0; cleanup_index < resolved.dependency_count; cleanup_index += 1)
    bb_context_deallocate(
      program->context,
      (void *)dependency_copies[cleanup_index].data,
      dependency_copies[cleanup_index].length,
      _Alignof(char)
    );
  if (dependency_copies != NULL)
    bb_context_deallocate(program->context, dependency_copies, dependency_bytes, _Alignof(bb_string_view));
  if (index != resolved.dependency_count) {
    bb_program_owned_asset_destroy(program, out_asset);
    return 0;
  }
  return 1;
}

static bb_semantic_value bb_program_call_asset(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value logical_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_asset_request request;
  bb_program_owned_asset resolved;
  bb_graph_asset_desc desc;
  bb_string_view logical_id;
  size_t index;
  bb_status status;
  if (call_info.child_count < 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "asset() expects a logical asset string.",
      call_info.span
    );
    return result;
  }
  logical_value = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (logical_value.type != BB_SEMANTIC_STRING || !bb_program_unquote(logical_value.data.string, &logical_id)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "asset() expects a string logical identifier.",
      call_info.span
    );
    bb_semantic_release(&logical_value);
    return result;
  }
  memset(&request, 0, sizeof(request));
  request.logical_id = logical_id;
  for (index = 2; index < call_info.child_count; index += 1) {
    const bb_syntax_node argument = bb_program_child(syntax, call, index);
    bb_syntax_node_info argument_info;
    bb_string_view name;
    bb_semantic_value value;
    (void)bb_program_node_info(syntax, argument, &argument_info);
    if (argument_info.kind != BB_SYNTAX_NAMED_ARGUMENT) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
        "Additional asset() constraints must be named.",
        argument_info.span
      );
      goto cleanup;
    }
    name = bb_program_node_text(syntax, bb_program_child(syntax, argument, 0));
    value = bb_program_evaluate(program, syntax, bb_program_child(syntax, argument, 1));
    if (bb_string_view_is(name, "hash") && value.type == BB_SEMANTIC_STRING) {
      if (!bb_program_unquote(value.data.string, &request.expected_content_id)) {
        bb_semantic_release(&value);
        goto constraint_error;
      }
    } else if (bb_string_view_is(name, "width") && value.type == BB_SEMANTIC_INTEGER &&
               value.data.integer > 0 && value.data.integer <= UINT32_MAX) {
      request.expected_width = (uint32_t)value.data.integer;
    } else if (bb_string_view_is(name, "height") && value.type == BB_SEMANTIC_INTEGER &&
               value.data.integer > 0 && value.data.integer <= UINT32_MAX) {
      request.expected_height = (uint32_t)value.data.integer;
    } else {
      bb_semantic_release(&value);
      goto constraint_error;
    }
    bb_semantic_release(&value);
  }
  if ((request.expected_width == 0) != (request.expected_height == 0)) goto constraint_error;
  request.has_expected_dimensions = request.expected_width != 0;
  if (!bb_program_resolve_asset_recursive(program, &request, call_info.span, NULL, 1, &resolved)) goto cleanup;
  desc.content_id = (bb_string_view){resolved.content_id, resolved.content_id_bytes};
  desc.width = resolved.width;
  desc.height = resolved.height;
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = resolved.width;
  result.data.image.height = resolved.height;
  result.data.image.encoded_alias = resolved.has_encoded_bytes;
  result.data.image.key = logical_id;
  result.data.image.path = logical_id;
  status = bb_image_graph_add_asset(program->graph, &desc, &result.data.image.node);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }
  bb_program_owned_asset_destroy(program, &resolved);
  goto cleanup;

constraint_error:
  bb_program_diagnostic_span(
    program,
    BB_SEMANTIC_DIAGNOSTIC_ASSET_CONSTRAINT,
    "asset() constraints are hash: string and paired positive width:/height: integers.",
    call_info.span
  );

cleanup:
  bb_semantic_release(&logical_value);
  return result;
}

static bb_semantic_value bb_program_call_fill(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value color = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_status status;
  if (call_info.child_count != 2) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "fill() expects one color.",
      call_info.span
    );
    return result;
  }
  color = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  if (color.type != BB_SEMANTIC_COLOR) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "fill() expects a color.",
      call_info.span
    );
    goto cleanup;
  }
  result.type = BB_SEMANTIC_IMAGE;
  result.data.image.width = 1;
  result.data.image.height = 1;
  status = bb_image_graph_add_fill(program->graph, 1, 1, color.data.color, &result.data.image.node);
  if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, call_info.span);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  bb_semantic_release(&color);
  return result;
}

static bb_semantic_value bb_program_call_artifact(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
) {
  bb_semantic_value path = bb_semantic_error();
  bb_semantic_value image = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  bb_string_view logical_path;
  if (call_info.child_count != 3) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT,
      "artifact() expects a logical path and an image.",
      call_info.span
    );
    return result;
  }
  path = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 1));
  image = bb_program_evaluate(program, syntax, bb_program_child(syntax, call, 2));
  if (path.type != BB_SEMANTIC_STRING || !bb_program_unquote(path.data.string, &logical_path) ||
      image.type != BB_SEMANTIC_IMAGE) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "artifact() expects a string logical path and an image.",
      call_info.span
    );
    goto cleanup;
  }
  result = image;
  result.data.image.key = logical_path;
  result.data.image.path = logical_path;

cleanup:
  bb_semantic_release(&image);
  bb_semantic_release(&path);
  return result;
}

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
  desc.easing = BB_EASING_LEGACY;
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
    desc.legacy_radial_rounding = 1;
  } else if (spec->geometry == 5) {
    desc.metric = BB_ALPHA_METRIC_EUCLIDEAN;
    desc.center_x = 32;
    desc.center_y = 32;
    desc.radius = 32;
    desc.direction = BB_ALPHA_DIRECTION_OUT;
    desc.legacy_radial_rounding = 1;
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

static bb_semantic_value bb_program_evaluate_call(
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
         callable.data.callable == BB_SEMANTIC_CALLABLE_MASK_PAIR))) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "map() expects palette/fill or artifact-product/mask-pair.",
      member_info.span
    );
    bb_semantic_release(&callable);
    return result;
  }
  state = bb_program_new_callback(
    program,
    callable.data.callable == BB_SEMANTIC_CALLABLE_FILL ? BB_PROGRAM_CALLBACK_FILL
                                                        : BB_PROGRAM_CALLBACK_MASK_PAIR,
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
  bb_semantic_value width_value = bb_semantic_error();
  bb_semantic_value height_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  uint32_t width;
  uint32_t height;
  bb_status status;
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
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result = receiver;
    if (receiver.data.image.width != width || receiver.data.image.height != height) {
      status = bb_image_graph_add_resize(program->graph, receiver.data.image.node, width, height, &result.data.image.node);
      if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
      if (status != BB_STATUS_OK) {
        bb_program_fail(program, status);
        result = bb_semantic_error();
        goto cleanup;
      }
      result.data.image.width = width;
      result.data.image.height = height;
    }
  } else if (receiver.type == BB_SEMANTIC_COLLECTION &&
             receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_SIZE, member_info.span);
    bb_collection *plan = NULL;
    if (state == NULL) goto cleanup;
    state->width = width;
    state->height = height;
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status != BB_STATUS_OK) {
      bb_program_fail(program, status);
      goto cleanup;
    }
    result.type = BB_SEMANTIC_COLLECTION;
    result.data.collection.plan = plan;
    result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
  } else {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "size() requires an image or image collection.",
      member_info.span
    );
  }

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
  bb_semantic_value value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  double opacity;
  bb_status status;
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
  if ((receiver.type != BB_SEMANTIC_IMAGE &&
       !(receiver.type == BB_SEMANTIC_COLLECTION && receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS)) ||
      !bb_program_opacity_value(value, &opacity)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "opacity() requires an image and a value in 0..1 or 0%..100%.",
      member_info.span
    );
    goto cleanup;
  }
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result = receiver;
    status = bb_image_graph_add_opacity(program->graph, receiver.data.image.node, opacity, &result.data.image.node);
    if (status == BB_STATUS_OK)
      status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
  } else {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_OPACITY, member_info.span);
    bb_collection *plan = NULL;
    if (state == NULL) goto cleanup;
    state->opacity = opacity;
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status == BB_STATUS_OK) {
      result.type = BB_SEMANTIC_COLLECTION;
      result.data.collection.plan = plan;
      result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
    }
  }
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

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
  bb_semantic_value turns_value = bb_semantic_error();
  bb_semantic_value result = bb_semantic_error();
  int32_t turns;
  bb_status status;
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
  if ((receiver.type != BB_SEMANTIC_IMAGE &&
       !(receiver.type == BB_SEMANTIC_COLLECTION && receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS)) ||
      !bb_program_i32(turns_value, &turns)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "rotate() requires an image and an integer quarter-turn count.",
      member_info.span
    );
    goto cleanup;
  }
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result = receiver;
    status = bb_image_graph_add_rotate(program->graph, receiver.data.image.node, turns, &result.data.image.node);
    if (status == BB_STATUS_OK)
      status = bb_image_graph_node_dimensions(
        program->graph,
        result.data.image.node,
        &result.data.image.width,
        &result.data.image.height
      );
    if (status == BB_STATUS_OK)
      status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
  } else {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_ROTATE, member_info.span);
    bb_collection *plan = NULL;
    if (state == NULL) goto cleanup;
    state->turns = turns;
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status == BB_STATUS_OK) {
      result.type = BB_SEMANTIC_COLLECTION;
      result.data.collection.plan = plan;
      result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
    }
  }
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

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
  bb_semantic_value arguments[4];
  bb_semantic_value result = bb_semantic_error();
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  bb_status status;
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
  if ((receiver.type != BB_SEMANTIC_IMAGE &&
       !(receiver.type == BB_SEMANTIC_COLLECTION && receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS)) ||
      !bb_program_i32(arguments[0], &x) ||
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
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result.type = BB_SEMANTIC_IMAGE;
    result.data.image.width = width;
    result.data.image.height = height;
    status = bb_image_graph_add_crop(program->graph, receiver.data.image.node, x, y, width, height, &result.data.image.node);
    if (status == BB_STATUS_OK)
      status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
  } else {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_CROP, member_info.span);
    bb_collection *plan = NULL;
    if (state == NULL) goto cleanup;
    state->x = x;
    state->y = y;
    state->width = width;
    state->height = height;
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status == BB_STATUS_OK) {
      result.type = BB_SEMANTIC_COLLECTION;
      result.data.collection.plan = plan;
      result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
    }
  }
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

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
  bb_semantic_value arguments[4];
  bb_semantic_value result = bb_semantic_error();
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
  bb_status status;
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
  if ((receiver.type != BB_SEMANTIC_IMAGE &&
       !(receiver.type == BB_SEMANTIC_COLLECTION && receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS)) ||
      !bb_program_positive_dimension(arguments[0], &width) ||
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
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result.type = BB_SEMANTIC_IMAGE;
    result.data.image.width = width;
    result.data.image.height = height;
    status = bb_image_graph_add_canvas(program->graph, receiver.data.image.node, width, height, x, y, &result.data.image.node);
    if (status == BB_STATUS_OK)
      status = bb_image_graph_attach_span(program->graph, result.data.image.node, member_info.span);
  } else {
    bb_program_callback_state *state = bb_program_new_callback(program, BB_PROGRAM_CALLBACK_CANVAS, member_info.span);
    bb_collection *plan = NULL;
    if (state == NULL) goto cleanup;
    state->width = width;
    state->height = height;
    state->x = x;
    state->y = y;
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status == BB_STATUS_OK) {
      result.type = BB_SEMANTIC_COLLECTION;
      result.data.collection.plan = plan;
      result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
    }
  }
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    result = bb_semantic_error();
  }

cleanup:
  for (index = 0; index < 4; index += 1) bb_semantic_release(&arguments[index]);
  return result;
}

static bb_status bb_program_add_unary_transform(
  const bb_program_callback_state *state,
  bb_image_node source,
  bb_image_node *out_node
) {
  switch (state->kind) {
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

static bb_semantic_value bb_program_lift_unary_transform(
  bb_program *program,
  bb_semantic_value receiver,
  const bb_program_callback_state *configuration,
  bb_span span
) {
  bb_semantic_value result = bb_semantic_error();
  bb_status status;
  if (receiver.type == BB_SEMANTIC_IMAGE) {
    result = receiver;
    result.data.image.encoded_alias = 0;
    status = bb_program_add_unary_transform(configuration, receiver.data.image.node, &result.data.image.node);
    if (status == BB_STATUS_OK) status = bb_image_graph_attach_span(program->graph, result.data.image.node, span);
  } else if (receiver.type == BB_SEMANTIC_COLLECTION &&
             receiver.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    bb_program_callback_state *state = bb_program_new_callback(program, configuration->kind, span);
    bb_collection *plan = NULL;
    if (state == NULL) return result;
    memcpy(state->colors, configuration->colors, sizeof(state->colors));
    status = bb_collection_map(receiver.data.collection.plan, 1, bb_program_map_callback, state, &plan);
    if (status == BB_STATUS_OK) {
      result.type = BB_SEMANTIC_COLLECTION;
      result.data.collection.plan = plan;
      result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;
    }
  } else {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "Image transform requires an image or image collection.",
      span
    );
    return result;
  }
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    bb_semantic_release(&result);
  }
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
  return bb_program_lift_unary_transform(program, receiver, &configuration, member_info.span);
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
  result = bb_program_lift_unary_transform(program, receiver, &configuration, member_info.span);

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
  result = bb_program_lift_unary_transform(program, receiver, &configuration, member_info.span);

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
  result = bb_program_lift_unary_transform(program, receiver, &configuration, member_info.span);

cleanup:
  for (index = 0; index < 2; index += 1) bb_semantic_release(&colors[index]);
  return result;
}

static bb_semantic_value bb_program_evaluate_member(
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

static bb_semantic_value bb_program_evaluate_array(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node array,
  bb_syntax_node_info array_info
) {
  bb_semantic_value result = bb_semantic_error();
  bb_value *artifacts = NULL;
  bb_collection *collection = NULL;
  size_t bytes;
  size_t index;
  bb_status status;
  if (array_info.child_count == 2) {
    bb_syntax_node_info left_info;
    bb_syntax_node_info right_info;
    const bb_syntax_node left_node = bb_program_child(syntax, array, 0);
    const bb_syntax_node right_node = bb_program_child(syntax, array, 1);
    if (bb_program_node_info(syntax, left_node, &left_info) == BB_STATUS_OK &&
        bb_program_node_info(syntax, right_node, &right_info) == BB_STATUS_OK &&
        left_info.kind == BB_SYNTAX_NUMBER_LITERAL && right_info.kind == BB_SYNTAX_NUMBER_LITERAL) {
      bb_semantic_value left = bb_program_evaluate(program, syntax, left_node);
      bb_semantic_value right = bb_program_evaluate(program, syntax, right_node);
      double x;
      double y;
      if (bb_semantic_as_number(left, &x) && bb_semantic_as_number(right, &y)) {
        result.type = BB_SEMANTIC_VECTOR2;
        result.data.vector2.x = x;
        result.data.vector2.y = y;
      } else {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Vector components must be finite unitless numbers.",
          array_info.span
        );
      }
      bb_semantic_release(&right);
      bb_semantic_release(&left);
      return result;
    }
  }
  if (array_info.child_count == 0) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
      "The initial compiler requires a typed non-empty array.",
      array_info.span
    );
    return result;
  }
  if (!bb_size_multiply(array_info.child_count, sizeof(*artifacts), &bytes)) {
    bb_program_fail(program, BB_STATUS_OVERFLOW);
    return result;
  }
  status = bb_context_allocate(program->context, bytes, _Alignof(bb_value), (void **)&artifacts);
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return result;
  }
  memset(artifacts, 0, bytes);
  for (index = 0; index < array_info.child_count; index += 1) {
    bb_semantic_value item = bb_program_evaluate(program, syntax, bb_program_child(syntax, array, index));
    bb_graph_asset_desc asset_desc;
    if (item.type != BB_SEMANTIC_IMAGE || item.data.image.key.length == 0) {
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "Array image items require stable keys; asset() provides one.",
        array_info.span
      );
      bb_semantic_release(&item);
      goto cleanup;
    }
    artifacts[index].kind = BB_VALUE_ARTIFACT;
    artifacts[index].data.artifact.key = item.data.image.key;
    artifacts[index].data.artifact.path = item.data.image.path;
    artifacts[index].data.artifact.image = item.data.image.node;
    artifacts[index].data.artifact.alias_identity = BB_ALIAS_NONE;
    artifacts[index].data.artifact.provenance = array_info.span;
    if (item.data.image.encoded_alias && program->options.encoded_asset != NULL &&
        bb_image_graph_asset(program->graph, item.data.image.node, &asset_desc) == BB_STATUS_OK) {
      artifacts[index].data.artifact.alias_identity = BB_ALIAS_BYTES;
      artifacts[index].data.artifact.alias_target = asset_desc.content_id;
    }
    bb_semantic_release(&item);
  }
  status = bb_collection_from_values(
    program->context,
    artifacts,
    array_info.child_count,
    &collection
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    goto cleanup;
  }
  result.type = BB_SEMANTIC_COLLECTION;
  result.data.collection.plan = collection;
  result.data.collection.shape = BB_SEMANTIC_COLLECTION_ARTIFACTS;

cleanup:
  bb_context_deallocate(program->context, artifacts, bytes, _Alignof(bb_value));
  return result;
}

static bb_semantic_value bb_program_evaluate(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node node
) {
  bb_syntax_node_info info;
  bb_semantic_value value = bb_semantic_error();
  bb_syntax_token_info token;
  if (program->status != BB_STATUS_OK || bb_program_node_info(syntax, node, &info) != BB_STATUS_OK) return value;
  switch (info.kind) {
    case BB_SYNTAX_NUMBER_LITERAL: {
      double number;
      int64_t integer;
      int is_integer;
      if (bb_syntax_tree_token(syntax, info.primary_token, &token) != BB_STATUS_OK ||
          !bb_parse_numeric_token(token, &number, &integer, &is_integer)) {
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
          "Invalid finite numeric literal.",
          info.span
        );
        break;
      }
      if (token.unit == BB_SYNTAX_UNIT_DEGREES) {
        value.type = BB_SEMANTIC_DEGREES;
        value.data.number = number;
      } else if (token.unit == BB_SYNTAX_UNIT_PERCENTAGE) {
        value.type = BB_SEMANTIC_PERCENTAGE;
        value.data.number = number;
      } else if (is_integer) {
        value.type = BB_SEMANTIC_INTEGER;
        value.data.integer = integer;
      } else {
        value.type = BB_SEMANTIC_NUMBER;
        value.data.number = number;
      }
      break;
    }
    case BB_SYNTAX_COLOR_LITERAL:
      value.type = BB_SEMANTIC_COLOR;
      if (!bb_parse_color(bb_program_token_text(syntax, info.primary_token), &value.data.color))
        value = bb_semantic_error();
      break;
    case BB_SYNTAX_STRING_LITERAL:
      value.type = BB_SEMANTIC_STRING;
      value.data.string = bb_program_token_text(syntax, info.primary_token);
      break;
    case BB_SYNTAX_IDENTIFIER_EXPRESSION: {
      const bb_string_view name = bb_program_token_text(syntax, info.primary_token);
      bb_program_binding *binding = bb_program_find_binding(program, name);
      value = binding != NULL ? bb_program_evaluate_binding(program, syntax, binding, info.span)
                              : bb_program_builtin_identifier(program, name, info.span);
      break;
    }
    case BB_SYNTAX_CALL_EXPRESSION:
      value = bb_program_evaluate_call(program, syntax, node, info);
      break;
    case BB_SYNTAX_MEMBER_CALL_EXPRESSION:
      value = bb_program_evaluate_member(program, syntax, node, info);
      break;
    case BB_SYNTAX_GROUP_EXPRESSION:
      value = bb_program_evaluate(program, syntax, bb_program_child(syntax, node, 0));
      break;
    case BB_SYNTAX_ARRAY_EXPRESSION:
      value = bb_program_evaluate_array(program, syntax, node, info);
      break;
    case BB_SYNTAX_ERROR_NODE:
      break;
    default:
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH,
        "This syntax form is not a value in the initial semantic slice.",
        info.span
      );
      break;
  }
  bb_program_trace_value(program, info.span, &value);
  return value;
}

static int bb_program_unquote_is(bb_string_view string, const char *expected) {
  bb_string_view unquoted;
  return bb_program_unquote(string, &unquoted) && bb_string_view_is(unquoted, expected);
}

static void bb_program_copy_syntax_diagnostics(bb_program *program, const bb_syntax_tree *syntax) {
  size_t index;
  for (index = 0; index < bb_syntax_tree_diagnostic_count(syntax) && program->status == BB_STATUS_OK; index += 1) {
    bb_diagnostic diagnostic;
    bb_status status = bb_syntax_tree_diagnostic(syntax, index, &diagnostic);
    if (status == BB_STATUS_OK)
      status = bb_diagnostic_store_push(
        &program->diagnostics,
        diagnostic.severity,
        diagnostic.code,
        diagnostic.message,
        diagnostic.primary_span,
        diagnostic.related_spans,
        diagnostic.related_span_count
      );
    bb_program_fail(program, status);
  }
}

static size_t bb_program_find_module_record(bb_program *program, bb_string_view identity) {
  size_t index;
  for (index = 0; index < program->module_count; index += 1) {
    const bb_program_module_record *record = &program->modules[index];
    if (record->identity_bytes == identity.length &&
        memcmp(record->identity, identity.data, identity.length) == 0) return index;
  }
  return SIZE_MAX;
}

static void bb_program_resolve_module_specifier(
  bb_program *program,
  bb_string_view specifier,
  bb_string_view importer_identity,
  bb_span span,
  uint32_t depth
) {
  bb_module_request request;
  bb_resolved_module resolved;
  bb_program_module_record *record;
  bb_string_view owned_identity;
  size_t record_index;
  bb_status status;
  if (program->status != BB_STATUS_OK) return;
  if (depth > program->limits.max_import_depth) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_IMPORT_DEPTH,
      "Module import depth limit exceeded.",
      span
    );
    return;
  }
  if (program->options.resolve_module == NULL) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT,
      "No host module resolver accepted this import.",
      span
    );
    return;
  }
  request.specifier = specifier;
  request.importer_identity = importer_identity;
  memset(&resolved, 0, sizeof(resolved));
  status = program->options.resolve_module(program->options.user, &request, &resolved);
  if (status != BB_STATUS_OK || resolved.identity.length == 0 || resolved.identity.data == NULL ||
      (resolved.kind != BB_RESOLVED_MODULE_SOURCE && resolved.kind != BB_RESOLVED_MODULE_PRECOMPILED)) {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT,
      "The host could not resolve this module.",
      span
    );
    return;
  }
  record_index = bb_program_find_module_record(program, resolved.identity);
  if (record_index != SIZE_MAX) {
    if (program->modules[record_index].state == 1)
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_MODULE_CYCLE,
        "Module import cycle detected.",
        span
      );
    return;
  }
  status = bb_program_grow(
    program,
    (void **)&program->modules,
    &program->module_capacity,
    sizeof(*program->modules),
    _Alignof(bb_program_module_record),
    program->module_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  record_index = program->module_count;
  record = &program->modules[record_index];
  memset(record, 0, sizeof(*record));
  record->identity_bytes = resolved.identity.length;
  status = bb_context_allocate(
    program->context,
    record->identity_bytes,
    _Alignof(char),
    (void **)&record->identity
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  memcpy(record->identity, resolved.identity.data, record->identity_bytes);
  record->state = 1;
  program->module_count += 1;
  owned_identity = (bb_string_view){record->identity, record->identity_bytes};
  {
    bb_bytes module_source = resolved.source;
    bb_string_view source_name = resolved.source_name.length == 0 ? owned_identity : resolved.source_name;
    int source_valid = 1;
    bb_source_id source_id = BB_SOURCE_ID_NONE;
    bb_syntax_tree *syntax = NULL;
    bb_syntax_node root;
    bb_syntax_node_info root_info;
    size_t index;
    if (resolved.kind == BB_RESOLVED_MODULE_PRECOMPILED) {
      bb_precompiled_module_info module_info;
      status = bb_precompiled_module_read(resolved.precompiled, &module_info, &module_source);
      if (status != BB_STATUS_OK) {
        source_valid = 0;
        bb_program_diagnostic_span(
          program,
          BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT,
          "Resolved precompiled module has an invalid or unsupported envelope.",
          span
        );
      }
    }
    if (source_valid && ((module_source.length != 0 && module_source.data == NULL) ||
        (source_name.length != 0 && source_name.data == NULL))) {
      source_valid = 0;
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT,
        "Resolved source module has invalid bytes.",
        span
      );
    }
    if (source_valid) {
      status = bb_context_add_source(program->context, source_name, module_source, &source_id);
      if (status == BB_STATUS_OK) status = bb_syntax_parse(program->context, source_id, &syntax);
      if (status != BB_STATUS_OK) bb_program_fail(program, status);
      if (syntax != NULL) {
        bb_program_copy_syntax_diagnostics(program, syntax);
        root = bb_syntax_tree_root(syntax);
        if (bb_program_node_info(syntax, root, &root_info) == BB_STATUS_OK) {
          for (index = 0; index < root_info.child_count && program->status == BB_STATUS_OK; index += 1) {
            const bb_syntax_node statement = bb_program_child(syntax, root, index);
            bb_syntax_node_info statement_info;
            (void)bb_program_node_info(syntax, statement, &statement_info);
            if (statement_info.kind == BB_SYNTAX_IMPORT_STATEMENT) {
              bb_string_view nested;
              const bb_string_view raw = bb_program_node_text(
                syntax,
                bb_program_child(syntax, statement, 0)
              );
              if (!bb_program_unquote(raw, &nested)) continue;
              if (bb_string_view_is(nested, "binblock/basic")) program->has_basic_module = 1;
              else if (bb_string_view_is(nested, "binblock/reference-set")) program->has_reference_module = 1;
              else
                bb_program_resolve_module_specifier(
                  program,
                  nested,
                  owned_identity,
                  statement_info.span,
                  depth + 1
                );
            }
          }
        }
        bb_syntax_tree_destroy(syntax);
      }
    }
  }
  program->modules[record_index].state = 2;
}

static void bb_program_collect_import(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node statement,
  bb_syntax_node_info info
) {
  const bb_syntax_node path = bb_program_child(syntax, statement, 0);
  const bb_string_view text = bb_program_node_text(syntax, path);
  bb_string_view specifier;
  if (bb_program_unquote_is(text, "binblock/basic")) program->has_basic_module = 1;
  else if (bb_program_unquote_is(text, "binblock/reference-set")) {
    program->has_reference_module = 1;
  } else if (bb_program_unquote(text, &specifier))
    bb_program_resolve_module_specifier(program, specifier, (bb_string_view){NULL, 0}, info.span, 1);
}

static void bb_program_collect_binding(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node statement,
  bb_syntax_node_info info
) {
  bb_program_binding *binding;
  const bb_syntax_node name_node = bb_program_child(syntax, statement, 0);
  const bb_string_view name = bb_program_node_text(syntax, name_node);
  size_t index;
  bb_status status = bb_program_grow(
    program,
    (void **)&program->bindings,
    &program->binding_capacity,
    sizeof(*program->bindings),
    _Alignof(bb_program_binding),
    program->binding_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_program_fail(program, status);
    return;
  }
  binding = &program->bindings[program->binding_count];
  memset(binding, 0, sizeof(*binding));
  binding->name = name;
  binding->span = info.span;
  binding->expression = bb_program_child(syntax, statement, 1);
  for (index = 0; index < program->binding_count; index += 1) {
    if (bb_string_view_equal(program->bindings[index].name, name)) {
      binding->duplicate = 1;
      bb_program_diagnostic_span(
        program,
        BB_SEMANTIC_DIAGNOSTIC_DUPLICATE_BINDING,
        "Duplicate immutable binding.",
        info.span
      );
      break;
    }
  }
  program->binding_count += 1;
}

static void bb_program_mark_output_trace(bb_program *program, bb_span span, size_t output_index) {
  size_t index;
  for (index = 0; index < program->trace_count; index += 1) {
    bb_semantic_trace *trace = &program->traces[index];
    if (trace->span.source_id == span.source_id && trace->span.byte_start == span.byte_start &&
        trace->span.byte_end == span.byte_end) {
      trace->output_index = output_index;
      return;
    }
  }
}

static void bb_program_add_output(
  bb_program *program,
  bb_semantic_value value,
  bb_span span
) {
  bb_collection *artifacts = NULL;
  bb_program_output_record *output;
  uint64_t cardinality;
  bb_status status;
  if (value.type == BB_SEMANTIC_IMAGE) {
    char key[32];
    bb_value artifact;
    const int length = snprintf(key, sizeof(key), "output-%lu", (unsigned long)program->output_count);
    memset(&artifact, 0, sizeof(artifact));
    artifact.kind = BB_VALUE_ARTIFACT;
    artifact.data.artifact.key = value.data.image.key.length == 0
                                   ? (bb_string_view){key, (size_t)length}
                                   : value.data.image.key;
    artifact.data.artifact.path = value.data.image.path.length == 0 ? artifact.data.artifact.key
                                                                    : value.data.image.path;
    artifact.data.artifact.image = value.data.image.node;
    artifact.data.artifact.alias_identity = BB_ALIAS_NONE;
    artifact.data.artifact.provenance = span;
    if (value.data.image.encoded_alias && program->options.encoded_asset != NULL) {
      bb_graph_asset_desc asset_desc;
      if (bb_image_graph_asset(program->graph, value.data.image.node, &asset_desc) == BB_STATUS_OK) {
        artifact.data.artifact.alias_identity = BB_ALIAS_BYTES;
        artifact.data.artifact.alias_target = asset_desc.content_id;
      }
    }
    status = bb_collection_from_values(program->context, &artifact, 1, &artifacts);
    if (status != BB_STATUS_OK) {
      bb_program_fail(program, status);
      return;
    }
  } else if (value.type == BB_SEMANTIC_COLLECTION &&
             value.data.collection.shape == BB_SEMANTIC_COLLECTION_ARTIFACTS) {
    artifacts = bb_collection_retain(value.data.collection.plan);
    if (artifacts == NULL) {
      bb_program_fail(program, BB_STATUS_LIMIT_EXCEEDED);
      return;
    }
  } else {
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_INVALID_OUTPUT,
      "A standalone output must be an image or image collection.",
      span
    );
    return;
  }
  status = bb_collection_count(artifacts, &cardinality);
  if (status != BB_STATUS_OK || cardinality > program->limits.max_output_count) {
    bb_collection_release(artifacts);
    bb_program_diagnostic_span(
      program,
      BB_SEMANTIC_DIAGNOSTIC_CARDINALITY,
      "Output cardinality is invalid or exceeds the configured output limit.",
      span
    );
    return;
  }
  status = bb_program_grow(
    program,
    (void **)&program->outputs,
    &program->output_capacity,
    sizeof(*program->outputs),
    _Alignof(bb_program_output_record),
    program->output_count + 1
  );
  if (status != BB_STATUS_OK) {
    bb_collection_release(artifacts);
    bb_program_fail(program, status);
    return;
  }
  output = &program->outputs[program->output_count];
  output->artifacts = artifacts;
  output->info.span = span;
  output->info.item_type = BB_SEMANTIC_ARTIFACT;
  output->info.cardinality = cardinality;
  bb_program_mark_output_trace(program, span, program->output_count);
  program->output_count += 1;
}

static bb_status bb_program_build_diagnostic_order(bb_program *program) {
  const size_t count = bb_diagnostic_store_count(&program->diagnostics);
  size_t bytes;
  size_t index;
  bb_status status;
  if (!bb_size_multiply(count, sizeof(*program->diagnostic_order), &bytes)) return BB_STATUS_OVERFLOW;
  program->diagnostic_order_bytes = bytes;
  status = bb_context_allocate(
    program->context,
    bytes,
    _Alignof(size_t),
    (void **)&program->diagnostic_order
  );
  if (status != BB_STATUS_OK) return status;
  for (index = 0; index < count; index += 1) {
    size_t position = index;
    bb_diagnostic current;
    program->diagnostic_order[index] = index;
    status = bb_diagnostic_store_get(&program->diagnostics, index, &current);
    if (status != BB_STATUS_OK) return status;
    while (position > 0) {
      bb_diagnostic previous;
      status = bb_diagnostic_store_get(&program->diagnostics, program->diagnostic_order[position - 1], &previous);
      if (status != BB_STATUS_OK) return status;
      if (previous.primary_span.source_id < current.primary_span.source_id ||
          (previous.primary_span.source_id == current.primary_span.source_id &&
           (previous.primary_span.byte_start < current.primary_span.byte_start ||
            (previous.primary_span.byte_start == current.primary_span.byte_start &&
             previous.primary_span.byte_end <= current.primary_span.byte_end)))) break;
      program->diagnostic_order[position] = program->diagnostic_order[position - 1];
      position -= 1;
    }
    program->diagnostic_order[position] = index;
  }
  return BB_STATUS_OK;
}

void bb_compile_options_init(bb_compile_options *options) {
  if (options == NULL) return;
  memset(options, 0, sizeof(*options));
  options->struct_size = (uint32_t)sizeof(*options);
}

bb_status bb_program_compile_with_options(
  bb_context *context,
  const bb_syntax_tree *syntax,
  const bb_compile_options *options,
  bb_program **out_program
) {
  bb_compile_options defaults;
  const bb_compile_options *effective_options = options;
  bb_program *program = NULL;
  bb_syntax_node root;
  bb_syntax_node_info root_info;
  size_t index;
  bb_status status;
  if (out_program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_program = NULL;
  if (context == NULL || syntax == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (effective_options == NULL) {
    bb_compile_options_init(&defaults);
    effective_options = &defaults;
  }
  if (effective_options->struct_size != sizeof(*effective_options)) return BB_STATUS_INVALID_ARGUMENT;
  if (effective_options->parameter_override_count != 0 && effective_options->parameter_overrides == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < effective_options->parameter_override_count; index += 1) {
    const bb_parameter_override *override = &effective_options->parameter_overrides[index];
    size_t previous;
    if (override->name.length == 0 || override->name.data == NULL ||
        (override->type != BB_SEMANTIC_BOOL && override->type != BB_SEMANTIC_INTEGER &&
         override->type != BB_SEMANTIC_NUMBER && override->type != BB_SEMANTIC_DEGREES &&
         override->type != BB_SEMANTIC_PERCENTAGE && override->type != BB_SEMANTIC_COLOR))
      return BB_STATUS_INVALID_ARGUMENT;
    for (previous = 0; previous < index; previous += 1)
      if (bb_string_view_equal(override->name, effective_options->parameter_overrides[previous].name))
        return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_context_allocate(context, sizeof(*program), _Alignof(bb_program), (void **)&program);
  if (status != BB_STATUS_OK) return status;
  memset(program, 0, sizeof(*program));
  program->context = context;
  program->options = *effective_options;
  program->status = BB_STATUS_OK;
  status = bb_context_get_limits(context, &program->limits);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_diagnostic_store_init(&program->diagnostics, context);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_image_graph_create(context, &program->graph);
  if (status != BB_STATUS_OK) goto fail;
  bb_program_copy_syntax_diagnostics(program, syntax);
  if (program->status != BB_STATUS_OK) {
    status = program->status;
    goto fail;
  }
  root = bb_syntax_tree_root(syntax);
  status = bb_program_node_info(syntax, root, &root_info);
  if (status != BB_STATUS_OK || root_info.kind != BB_SYNTAX_PROGRAM) {
    status = BB_STATUS_INVALID_ARGUMENT;
    goto fail;
  }
  for (index = 0; index < root_info.child_count && program->status == BB_STATUS_OK; index += 1) {
    const bb_syntax_node statement = bb_program_child(syntax, root, index);
    bb_syntax_node_info info;
    (void)bb_program_node_info(syntax, statement, &info);
    if (info.kind == BB_SYNTAX_IMPORT_STATEMENT) bb_program_collect_import(program, syntax, statement, info);
    else if (info.kind == BB_SYNTAX_BINDING_STATEMENT) bb_program_collect_binding(program, syntax, statement, info);
  }
  for (index = 0; index < program->binding_count && program->status == BB_STATUS_OK; index += 1) {
    bb_semantic_value value;
    if (program->bindings[index].duplicate) continue;
    value = bb_program_evaluate_binding(program, syntax, &program->bindings[index], program->bindings[index].span);
    bb_semantic_release(&value);
  }
  for (index = 0; index < root_info.child_count && program->status == BB_STATUS_OK; index += 1) {
    const bb_syntax_node statement = bb_program_child(syntax, root, index);
    bb_syntax_node_info info;
    (void)bb_program_node_info(syntax, statement, &info);
    if (info.kind == BB_SYNTAX_EXPRESSION_STATEMENT) {
      const bb_syntax_node expression = bb_program_child(syntax, statement, 0);
      bb_syntax_node_info expression_info;
      bb_semantic_value value;
      (void)bb_program_node_info(syntax, expression, &expression_info);
      value = bb_program_evaluate(program, syntax, expression);
      bb_program_add_output(program, value, expression_info.span);
      bb_semantic_release(&value);
    }
  }
  if (program->status != BB_STATUS_OK) {
    status = program->status;
    goto fail;
  }
  /* Overrides are a compile-time borrowed view. Resolvers and their user data
   * remain available for lazy asset decode, but override storage must not be
   * retained by the compiled program. */
  program->options.parameter_overrides = NULL;
  program->options.parameter_override_count = 0;
  status = bb_program_build_diagnostic_order(program);
  if (status != BB_STATUS_OK) goto fail;
  *out_program = program;
  return BB_STATUS_OK;

fail:
  bb_program_destroy(program);
  return status;
}

bb_status bb_program_compile(bb_context *context, const bb_syntax_tree *syntax, bb_program **out_program) {
  return bb_program_compile_with_options(context, syntax, NULL, out_program);
}

void bb_program_destroy(bb_program *program) {
  bb_program_callback_state *callback;
  bb_context *context;
  size_t index;
  size_t bytes;
  if (program == NULL) return;
  context = program->context;
  for (index = 0; index < program->output_count; index += 1)
    bb_collection_release(program->outputs[index].artifacts);
  for (index = 0; index < program->binding_count; index += 1)
    bb_semantic_release(&program->bindings[index].value);
  for (index = 0; index < program->module_count; index += 1)
    bb_context_deallocate(
      context,
      program->modules[index].identity,
      program->modules[index].identity_bytes,
      _Alignof(char)
    );
  callback = program->callbacks;
  while (callback != NULL) {
    bb_program_callback_state *next = callback->next;
    bb_context_deallocate(context, callback, sizeof(*callback), _Alignof(bb_program_callback_state));
    callback = next;
  }
  bb_image_graph_destroy(program->graph);
  bb_context_deallocate(context, program->diagnostic_order, program->diagnostic_order_bytes, _Alignof(size_t));
  bb_diagnostic_store_destroy(&program->diagnostics);
  bytes = program->output_capacity * sizeof(*program->outputs);
  bb_context_deallocate(context, program->outputs, bytes, _Alignof(bb_program_output_record));
  bytes = program->trace_capacity * sizeof(*program->traces);
  bb_context_deallocate(context, program->traces, bytes, _Alignof(bb_semantic_trace));
  bytes = program->binding_capacity * sizeof(*program->bindings);
  bb_context_deallocate(context, program->bindings, bytes, _Alignof(bb_program_binding));
  bytes = program->module_capacity * sizeof(*program->modules);
  bb_context_deallocate(context, program->modules, bytes, _Alignof(bb_program_module_record));
  bb_context_deallocate(context, program, sizeof(*program), _Alignof(bb_program));
}

const bb_image_graph *bb_program_image_graph(const bb_program *program) {
  return program == NULL ? NULL : program->graph;
}

size_t bb_program_diagnostic_count(const bb_program *program) {
  return program == NULL ? 0 : bb_diagnostic_store_count(&program->diagnostics);
}

bb_status bb_program_diagnostic(const bb_program *program, size_t index, bb_diagnostic *out_diagnostic) {
  if (program == NULL || out_diagnostic == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= bb_program_diagnostic_count(program)) return BB_STATUS_NOT_FOUND;
  return bb_diagnostic_store_get(&program->diagnostics, program->diagnostic_order[index], out_diagnostic);
}

size_t bb_program_trace_count(const bb_program *program) {
  return program == NULL ? 0 : program->trace_count;
}

bb_status bb_program_trace(const bb_program *program, size_t index, bb_semantic_trace *out_trace) {
  if (program == NULL || out_trace == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (index >= program->trace_count) return BB_STATUS_NOT_FOUND;
  *out_trace = program->traces[index];
  return BB_STATUS_OK;
}

bb_status bb_program_trace_at(
  const bb_program *program,
  bb_source_id source_id,
  uint32_t byte_offset,
  bb_semantic_trace *out_trace
) {
  size_t best = SIZE_MAX;
  uint32_t best_length = UINT32_MAX;
  size_t index;
  if (program == NULL || out_trace == NULL || source_id == BB_SOURCE_ID_NONE) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < program->trace_count; index += 1) {
    const bb_semantic_trace *trace = &program->traces[index];
    const uint32_t length = trace->span.byte_end - trace->span.byte_start;
    if (trace->span.source_id == source_id && byte_offset >= trace->span.byte_start &&
        byte_offset <= trace->span.byte_end && length <= best_length) {
      best = index;
      best_length = length;
    }
  }
  if (best == SIZE_MAX) return BB_STATUS_NOT_FOUND;
  *out_trace = program->traces[best];
  return BB_STATUS_OK;
}

size_t bb_program_output_count(const bb_program *program) {
  return program == NULL ? 0 : program->output_count;
}

size_t bb_program_parameter_count(const bb_program *program) {
  size_t count = 0;
  size_t index;
  if (program == NULL) return 0;
  for (index = 0; index < program->binding_count; index += 1)
    if (!program->bindings[index].duplicate && bb_program_value_is_parameter(&program->bindings[index].value)) count += 1;
  return count;
}

bb_status bb_program_parameter(
  const bb_program *program,
  size_t parameter_index,
  bb_program_parameter_info *out_info
) {
  size_t found = 0;
  size_t index;
  if (program == NULL || out_info == NULL) return BB_STATUS_INVALID_ARGUMENT;
  for (index = 0; index < program->binding_count; index += 1) {
    const bb_program_binding *binding = &program->bindings[index];
    if (binding->duplicate || !bb_program_value_is_parameter(&binding->value)) continue;
    if (found == parameter_index) {
      memset(out_info, 0, sizeof(*out_info));
      out_info->name = binding->name;
      out_info->span = binding->span;
      out_info->type = binding->value.type;
      if (binding->value.type == BB_SEMANTIC_BOOL)
        out_info->value.boolean = binding->value.data.boolean;
      else if (binding->value.type == BB_SEMANTIC_INTEGER)
        out_info->value.integer = binding->value.data.integer;
      else if (binding->value.type == BB_SEMANTIC_NUMBER || binding->value.type == BB_SEMANTIC_DEGREES ||
               binding->value.type == BB_SEMANTIC_PERCENTAGE)
        out_info->value.number = binding->value.data.number;
      else if (binding->value.type == BB_SEMANTIC_COLOR)
        out_info->value.color = binding->value.data.color;
      return BB_STATUS_OK;
    }
    found += 1;
  }
  return BB_STATUS_NOT_FOUND;
}

bb_status bb_program_output(const bb_program *program, size_t output_index, bb_program_output_info *out_info) {
  if (program == NULL || out_info == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (output_index >= program->output_count) return BB_STATUS_NOT_FOUND;
  *out_info = program->outputs[output_index].info;
  return BB_STATUS_OK;
}

bb_status bb_program_output_artifact(
  const bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_artifact_value *out_artifact
) {
  bb_value value;
  bb_status status;
  if (program == NULL || out_artifact == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (output_index >= program->output_count) return BB_STATUS_NOT_FOUND;
  status = bb_collection_get(program->outputs[output_index].artifacts, item_index, &value, 1);
  if (status != BB_STATUS_OK) return status;
  if (value.kind != BB_VALUE_ARTIFACT) return BB_STATUS_INTERNAL_ERROR;
  *out_artifact = value.data.artifact;
  return BB_STATUS_OK;
}

typedef struct bb_program_render_state {
  bb_program *program;
  const bb_render_options *options;
} bb_program_render_state;

static bb_status bb_program_artifact_renderer(
  void *user,
  bb_context *context,
  const bb_image_graph *graph,
  const bb_artifact_value *artifact,
  bb_surface **out_surface
) {
  bb_program_render_state *state = user;
  return bb_image_graph_render_raster_with_options(
    context,
    graph,
    artifact->image,
    state->options,
    state->program->options.decode_asset,
    state->program->options.user,
    out_surface
  );
}

bb_status bb_program_render_output_with_options(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  const bb_render_options *options,
  bb_surface **out_surface
) {
  bb_program_render_state state;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (program == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (options != NULL && options->struct_size != sizeof(*options)) return BB_STATUS_INVALID_ARGUMENT;
  if (options != NULL && options->should_cancel != NULL && options->should_cancel(options->user))
    return BB_STATUS_CANCELLED;
  if (output_index >= program->output_count) return BB_STATUS_NOT_FOUND;
  state.program = program;
  state.options = options;
  return bb_collection_render_artifact_with(
    program->context,
    program->outputs[output_index].artifacts,
    item_index,
    program->graph,
    bb_program_artifact_renderer,
    &state,
    out_surface
  );
}

bb_status bb_program_render_output(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_surface **out_surface
) {
  return bb_program_render_output_with_options(program, output_index, item_index, NULL, out_surface);
}

bb_status bb_program_output_encoded(
  const bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_bytes *out_bytes
) {
  bb_artifact_value artifact;
  bb_status status;
  if (out_bytes == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_bytes = (bb_bytes){NULL, 0};
  if (program == NULL || program->options.encoded_asset == NULL) return BB_STATUS_UNSUPPORTED;
  status = bb_program_output_artifact(program, output_index, item_index, &artifact);
  if (status != BB_STATUS_OK) return status;
  if (artifact.alias_identity != BB_ALIAS_BYTES || artifact.alias_target.length == 0) return BB_STATUS_UNSUPPORTED;
  return program->options.encoded_asset(program->options.user, artifact.alias_target, out_bytes);
}
