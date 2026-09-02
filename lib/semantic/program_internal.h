#ifndef BINBLOCK_PROGRAM_INTERNAL_H
#define BINBLOCK_PROGRAM_INTERNAL_H

#include <binblock/program.h>

#include "checked_math.h"
#include "context_internal.h"
#include "diagnostic.h"

typedef enum bb_semantic_collection_shape {
  BB_SEMANTIC_COLLECTION_NONE = 0,
  BB_SEMANTIC_COLLECTION_PALETTE = 1,
  BB_SEMANTIC_COLLECTION_ARTIFACTS = 2,
  BB_SEMANTIC_COLLECTION_ARTIFACT_PAIRS = 3
} bb_semantic_collection_shape;

typedef enum bb_semantic_callable {
  BB_SEMANTIC_CALLABLE_NONE = 0,
  BB_SEMANTIC_CALLABLE_FILL = 1,
  BB_SEMANTIC_CALLABLE_MASK_PAIR = 2,
  BB_SEMANTIC_CALLABLE_OVER_PAIR = 3
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
  BB_PROGRAM_CALLBACK_MASK_PAIR,
  BB_PROGRAM_CALLBACK_OVER_PAIR
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

bb_semantic_value bb_semantic_error(void);
bb_semantic_value bb_semantic_copy(bb_semantic_value value);
void bb_semantic_release(bb_semantic_value *value);
int bb_program_value_is_parameter(const bb_semantic_value *value);
bb_status bb_program_grow(
  bb_program *program,
  void **items,
  size_t *capacity,
  size_t item_size,
  size_t alignment,
  size_t required
);
void bb_program_fail(bb_program *program, bb_status status);
void bb_program_diagnostic_span(
  bb_program *program,
  bb_semantic_diagnostic_code code,
  const char *message,
  bb_span span
);
int bb_string_view_equal(bb_string_view left, bb_string_view right);
int bb_string_view_is(bb_string_view value, const char *text);
int bb_program_unquote(bb_string_view string, bb_string_view *out_value);
bb_status bb_program_node_info(
  const bb_syntax_tree *syntax,
  bb_syntax_node node,
  bb_syntax_node_info *out_info
);
bb_syntax_node bb_program_child(const bb_syntax_tree *syntax, bb_syntax_node node, size_t index);
bb_string_view bb_program_token_text(const bb_syntax_tree *syntax, uint32_t token_index);
bb_string_view bb_program_node_text(const bb_syntax_tree *syntax, bb_syntax_node node);
void bb_program_trace_value(
  bb_program *program,
  bb_span span,
  const bb_semantic_value *value
);
int bb_parse_color(bb_string_view text, bb_rgba8 *out_color);
int bb_parse_numeric_token(
  bb_syntax_token_info token,
  double *out_number,
  int64_t *out_integer,
  int *out_is_integer
);
int bb_semantic_as_number(bb_semantic_value value, double *out_number);
bb_program_binding *bb_program_find_binding(bb_program *program, bb_string_view name);
bb_semantic_value bb_program_evaluate_binding(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_program_binding *binding,
  bb_span use_span
);
bb_semantic_value bb_program_builtin_identifier(
  bb_program *program,
  bb_string_view name,
  bb_span span
);
bb_program_callback_state *bb_program_new_callback(
  bb_program *program,
  bb_program_callback_kind kind,
  bb_span span
);
bb_status bb_program_apply_image_transform(
  const bb_program_callback_state *state,
  bb_image_node source,
  bb_image_node *out_node
);
bb_status bb_program_map_callback(
  void *user,
  const bb_value *input,
  size_t input_count,
  bb_value *output,
  size_t output_count
);
bb_semantic_value bb_program_call_palette(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_call_linear_gradient(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_call_radial_gradient(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_call_asset(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_call_fill(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_call_artifact(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_evaluate_call(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node call,
  bb_syntax_node_info call_info
);
bb_semantic_value bb_program_evaluate_member(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node member,
  bb_syntax_node_info member_info
);
bb_semantic_value bb_program_evaluate(
  bb_program *program,
  const bb_syntax_tree *syntax,
  bb_syntax_node node
);

#endif
