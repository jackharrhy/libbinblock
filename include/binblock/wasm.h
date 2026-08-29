#ifndef BINBLOCK_WASM_H
#define BINBLOCK_WASM_H

#include <binblock/program.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__EMSCRIPTEN__)
#define BB_WASM_API __attribute__((used, visibility("default")))
#else
#define BB_WASM_API BB_API
#endif

typedef uint32_t bb_wasm_session;

#define BB_WASM_SESSION_NONE ((bb_wasm_session)0)

typedef struct bb_wasm_diagnostic_info {
  uint32_t struct_size;
  uint32_t severity;
  uint32_t code;
  uint32_t source_id;
  uint32_t byte_start;
  uint32_t byte_end;
  uint32_t message_length;
} bb_wasm_diagnostic_info;

typedef struct bb_wasm_trace_info {
  uint32_t struct_size;
  uint32_t type;
  uint32_t image;
  uint32_t output_index_low;
  uint32_t output_index_high;
  uint32_t source_id;
  uint32_t byte_start;
  uint32_t byte_end;
} bb_wasm_trace_info;

typedef struct bb_wasm_parameter_info {
  uint32_t struct_size;
  uint32_t type;
  uint32_t name_length;
  uint32_t source_id;
  uint32_t byte_start;
  uint32_t byte_end;
} bb_wasm_parameter_info;

typedef struct bb_wasm_output_info {
  uint32_t struct_size;
  uint32_t item_type;
  uint32_t cardinality_low;
  uint32_t cardinality_high;
  uint32_t source_id;
  uint32_t byte_start;
  uint32_t byte_end;
} bb_wasm_output_info;

typedef struct bb_wasm_artifact_info {
  uint32_t struct_size;
  uint32_t image;
  uint32_t key_length;
  uint32_t path_length;
  uint32_t alias_identity;
  uint32_t alias_target_length;
  uint32_t source_id;
  uint32_t byte_start;
  uint32_t byte_end;
} bb_wasm_artifact_info;

typedef struct bb_wasm_render_info {
  uint32_t struct_size;
  uint32_t width;
  uint32_t height;
  uint32_t rgba_length;
} bb_wasm_render_info;

typedef struct bb_wasm_graph_node_info {
  uint32_t struct_size;
  uint32_t kind;
  uint32_t width;
  uint32_t height;
  uint32_t input_count;
  uint32_t inputs[2];
  uint32_t options[6];
  uint32_t scalar_bits[12];
  uint32_t stop_count;
  uint32_t asset_content_id_length;
} bb_wasm_graph_node_info;

typedef struct bb_wasm_gradient_stop_info {
  uint32_t struct_size;
  uint32_t offset_low;
  uint32_t offset_high;
  uint32_t rgba;
  uint32_t easing;
  uint32_t has_easing;
} bb_wasm_gradient_stop_info;

/* The binding owns a bounded, single-threaded session table. All returned
 * handles are generation-checked integers; no core pointer crosses the ABI. */
BB_WASM_API bb_wasm_session bb_wasm_session_create(void);
BB_WASM_API void bb_wasm_session_destroy(bb_wasm_session session);
BB_WASM_API uint32_t bb_wasm_session_generation(bb_wasm_session session);
BB_WASM_API void bb_wasm_session_clear_resources(bb_wasm_session session);
BB_WASM_API bb_status bb_wasm_session_add_module(
  bb_wasm_session session,
  const uint8_t *specifier,
  uint32_t specifier_length,
  const uint8_t *identity,
  uint32_t identity_length,
  const uint8_t *source,
  uint32_t source_length
);
BB_WASM_API bb_status bb_wasm_session_add_asset_rgba(
  bb_wasm_session session,
  const uint8_t *logical_id,
  uint32_t logical_id_length,
  const uint8_t *content_id,
  uint32_t content_id_length,
  uint32_t width,
  uint32_t height,
  const uint8_t *rgba,
  uint32_t rgba_length,
  const uint8_t *encoded,
  uint32_t encoded_length
);
BB_WASM_API bb_status bb_wasm_session_add_asset_metadata(
  bb_wasm_session session,
  const uint8_t *logical_id,
  uint32_t logical_id_length,
  const uint8_t *content_id,
  uint32_t content_id_length,
  uint32_t width,
  uint32_t height,
  uint32_t has_encoded_bytes
);
BB_WASM_API bb_status bb_wasm_session_hydrate_asset(
  bb_wasm_session session,
  const uint8_t *logical_id,
  uint32_t logical_id_length,
  const uint8_t *rgba,
  uint32_t rgba_length,
  const uint8_t *encoded,
  uint32_t encoded_length
);
BB_WASM_API bb_status bb_wasm_session_compile(
  bb_wasm_session session,
  const uint8_t *source,
  uint32_t source_length
);

BB_WASM_API uint32_t bb_wasm_diagnostic_count(bb_wasm_session session);
BB_WASM_API bb_status bb_wasm_diagnostic_get(
  bb_wasm_session session,
  uint32_t diagnostic_index,
  bb_wasm_diagnostic_info *out_info
);
BB_WASM_API bb_status bb_wasm_diagnostic_copy_message(
  bb_wasm_session session,
  uint32_t diagnostic_index,
  uint8_t *destination,
  uint32_t capacity
);

BB_WASM_API bb_status bb_wasm_trace_at(
  bb_wasm_session session,
  uint32_t byte_offset,
  bb_wasm_trace_info *out_info
);

BB_WASM_API uint32_t bb_wasm_parameter_count(bb_wasm_session session);
BB_WASM_API bb_status bb_wasm_parameter_get(
  bb_wasm_session session,
  uint32_t parameter_index,
  bb_wasm_parameter_info *out_info
);
BB_WASM_API bb_status bb_wasm_parameter_copy_name(
  bb_wasm_session session,
  uint32_t parameter_index,
  uint8_t *destination,
  uint32_t capacity
);
BB_WASM_API bb_status bb_wasm_parameter_set_bool(
  bb_wasm_session session,
  uint32_t parameter_index,
  uint32_t value
);
BB_WASM_API bb_status bb_wasm_parameter_set_integer(
  bb_wasm_session session,
  uint32_t parameter_index,
  int32_t value_low,
  int32_t value_high
);
BB_WASM_API bb_status bb_wasm_parameter_set_number(
  bb_wasm_session session,
  uint32_t parameter_index,
  double value
);
BB_WASM_API bb_status bb_wasm_parameter_set_color(
  bb_wasm_session session,
  uint32_t parameter_index,
  uint32_t rgba
);

BB_WASM_API uint32_t bb_wasm_output_count(bb_wasm_session session);
BB_WASM_API bb_status bb_wasm_output_get(
  bb_wasm_session session,
  uint32_t output_index,
  bb_wasm_output_info *out_info
);
BB_WASM_API bb_status bb_wasm_artifact_get(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  bb_wasm_artifact_info *out_info
);
BB_WASM_API bb_status bb_wasm_artifact_copy_key(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  uint8_t *destination,
  uint32_t capacity
);
BB_WASM_API bb_status bb_wasm_artifact_copy_path(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  uint8_t *destination,
  uint32_t capacity
);
BB_WASM_API bb_status bb_wasm_graph_node_get(
  bb_wasm_session session,
  uint32_t node,
  bb_wasm_graph_node_info *out_info
);
BB_WASM_API bb_status bb_wasm_graph_asset_copy_content_id(
  bb_wasm_session session,
  uint32_t node,
  uint8_t *destination,
  uint32_t capacity
);
BB_WASM_API bb_status bb_wasm_graph_gradient_stop_get(
  bb_wasm_session session,
  uint32_t node,
  uint32_t stop_index,
  bb_wasm_gradient_stop_info *out_info
);
BB_WASM_API bb_status bb_wasm_render_output_rgba(
  bb_wasm_session session,
  uint32_t output_index,
  uint32_t item_index_low,
  uint32_t item_index_high,
  uint8_t *destination,
  uint32_t capacity,
  bb_wasm_render_info *out_info
);

#ifdef __cplusplus
}
#endif

#endif
