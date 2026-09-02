#ifndef BINBLOCK_PROGRAM_H
#define BINBLOCK_PROGRAM_H

#include <binblock/collection.h>
#include <binblock/syntax.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_program bb_program;

typedef struct bb_module_request {
  bb_string_view specifier;
  bb_string_view importer_identity;
} bb_module_request;

typedef struct bb_resolved_module {
  /* Stable content identity used for cycle detection and cache identity. */
  bb_string_view identity;
  bb_string_view source_name;
  bb_bytes source;
} bb_resolved_module;

typedef struct bb_asset_request {
  bb_string_view logical_id;
  bb_string_view expected_content_id;
  uint32_t expected_width;
  uint32_t expected_height;
  uint32_t has_expected_dimensions;
} bb_asset_request;

typedef struct bb_resolved_asset {
  /* Stable content identity, not a filesystem path. */
  bb_string_view content_id;
  uint32_t width;
  uint32_t height;
  uint32_t has_encoded_bytes;
  const bb_string_view *dependencies;
  size_t dependency_count;
} bb_resolved_asset;

typedef bb_status (*bb_module_resolve_fn)(
  void *user,
  const bb_module_request *request,
  bb_resolved_module *out_module
);
typedef bb_status (*bb_asset_resolve_fn)(
  void *user,
  const bb_asset_request *request,
  bb_resolved_asset *out_asset
);
typedef bb_status (*bb_asset_decode_fn)(
  void *user,
  bb_string_view content_id,
  bb_const_image_view *out_image
);
typedef bb_status (*bb_asset_encoded_fn)(
  void *user,
  bb_string_view content_id,
  bb_bytes *out_bytes
);

typedef enum bb_semantic_type {
  BB_SEMANTIC_ERROR = 0,
  BB_SEMANTIC_BOOL = 1,
  BB_SEMANTIC_INTEGER = 2,
  BB_SEMANTIC_NUMBER = 3,
  BB_SEMANTIC_STRING = 4,
  BB_SEMANTIC_COLOR = 5,
  BB_SEMANTIC_VECTOR2 = 6,
  BB_SEMANTIC_IMAGE = 7,
  BB_SEMANTIC_DEGREES = 8,
  BB_SEMANTIC_PERCENTAGE = 9,
  BB_SEMANTIC_ASSET = 10,
  BB_SEMANTIC_CALLABLE = 11,
  BB_SEMANTIC_ARTIFACT = 12,
  BB_SEMANTIC_COLLECTION = 13
} bb_semantic_type;

typedef struct bb_parameter_override {
  bb_string_view name;
  bb_semantic_type type;
  union {
    uint32_t boolean;
    int64_t integer;
    double number;
    bb_rgba8 color;
  } value;
} bb_parameter_override;

typedef struct bb_compile_options {
  uint32_t struct_size;
  void *user;
  bb_module_resolve_fn resolve_module;
  bb_asset_resolve_fn resolve_asset;
  bb_asset_decode_fn decode_asset;
  bb_asset_encoded_fn encoded_asset;
  /* Borrowed only for the duration of compilation. Overrides must have the
   * same scalar type as the source binding they replace. */
  const bb_parameter_override *parameter_overrides;
  size_t parameter_override_count;
} bb_compile_options;

typedef enum bb_semantic_diagnostic_code {
  BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_IMPORT = 2000,
  BB_SEMANTIC_DIAGNOSTIC_DUPLICATE_BINDING = 2001,
  BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_NAME = 2002,
  BB_SEMANTIC_DIAGNOSTIC_BINDING_CYCLE = 2003,
  BB_SEMANTIC_DIAGNOSTIC_TYPE_MISMATCH = 2004,
  BB_SEMANTIC_DIAGNOSTIC_ARGUMENT_COUNT = 2005,
  BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_FUNCTION = 2006,
  BB_SEMANTIC_DIAGNOSTIC_UNKNOWN_METHOD = 2007,
  BB_SEMANTIC_DIAGNOSTIC_INVALID_OUTPUT = 2008,
  BB_SEMANTIC_DIAGNOSTIC_CARDINALITY = 2009,
  BB_SEMANTIC_DIAGNOSTIC_MODULE_CYCLE = 2010,
  BB_SEMANTIC_DIAGNOSTIC_IMPORT_DEPTH = 2011,
  BB_SEMANTIC_DIAGNOSTIC_ASSET_RESOLUTION = 2012,
  BB_SEMANTIC_DIAGNOSTIC_ASSET_CYCLE = 2013,
  BB_SEMANTIC_DIAGNOSTIC_ASSET_CONSTRAINT = 2014
} bb_semantic_diagnostic_code;

typedef struct bb_semantic_trace {
  bb_span span;
  bb_semantic_type type;
  bb_image_node image;
  /* UINT64_MAX when this expression is not a standalone output. */
  uint64_t output_index;
} bb_semantic_trace;

typedef struct bb_program_output_info {
  bb_span span;
  bb_semantic_type item_type;
  uint64_t cardinality;
} bb_program_output_info;

typedef struct bb_program_parameter_info {
  bb_string_view name;
  bb_span span;
  bb_semantic_type type;
  union {
    uint32_t boolean;
    int64_t integer;
    double number;
    bb_rgba8 color;
  } value;
} bb_program_parameter_info;

/* Compilation performs syntax-independent name/type analysis and graph/plan
 * lowering. The syntax tree may be destroyed after this call returns. */
BB_API bb_status bb_program_compile(
  bb_context *context,
  const bb_syntax_tree *syntax,
  bb_program **out_program
);
BB_API void bb_compile_options_init(bb_compile_options *options);
BB_API bb_status bb_program_compile_with_options(
  bb_context *context,
  const bb_syntax_tree *syntax,
  const bb_compile_options *options,
  bb_program **out_program
);
BB_API void bb_program_destroy(bb_program *program);
BB_API const bb_image_graph *bb_program_image_graph(const bb_program *program);
BB_API size_t bb_program_diagnostic_count(const bb_program *program);
BB_API bb_status bb_program_diagnostic(
  const bb_program *program,
  size_t index,
  bb_diagnostic *out_diagnostic
);
BB_API size_t bb_program_trace_count(const bb_program *program);
BB_API bb_status bb_program_trace(
  const bb_program *program,
  size_t index,
  bb_semantic_trace *out_trace
);
BB_API bb_status bb_program_trace_at(
  const bb_program *program,
  bb_source_id source_id,
  uint32_t byte_offset,
  bb_semantic_trace *out_trace
);
BB_API size_t bb_program_output_count(const bb_program *program);
BB_API size_t bb_program_parameter_count(const bb_program *program);
BB_API bb_status bb_program_parameter(
  const bb_program *program,
  size_t parameter_index,
  bb_program_parameter_info *out_info
);
BB_API bb_status bb_program_output(
  const bb_program *program,
  size_t output_index,
  bb_program_output_info *out_info
);
BB_API bb_status bb_program_output_artifact(
  const bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_artifact_value *out_artifact
);
BB_API bb_status bb_program_render_output(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_surface **out_surface
);
BB_API bb_status bb_program_render_output_with_options(
  bb_program *program,
  size_t output_index,
  uint64_t item_index,
  const bb_render_options *options,
  bb_surface **out_surface
);
BB_API bb_status bb_program_output_encoded(
  const bb_program *program,
  size_t output_index,
  uint64_t item_index,
  bb_bytes *out_bytes
);

#ifdef __cplusplus
}
#endif

#endif
