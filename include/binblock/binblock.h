#ifndef BINBLOCK_BINBLOCK_H
#define BINBLOCK_BINBLOCK_H

#include <stddef.h>
#include <stdint.h>

#if defined(BB_STATIC)
#define BB_API
#elif defined(_WIN32)
#if defined(BB_IMPLEMENTATION)
#define BB_API __declspec(dllexport)
#else
#define BB_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define BB_API __attribute__((visibility("default")))
#else
#define BB_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_context bb_context;

typedef enum bb_status {
  BB_STATUS_OK = 0,
  BB_STATUS_INVALID_ARGUMENT = 1,
  BB_STATUS_OUT_OF_MEMORY = 2,
  BB_STATUS_OVERFLOW = 3,
  BB_STATUS_LIMIT_EXCEEDED = 4,
  BB_STATUS_UNSUPPORTED = 5,
  BB_STATUS_INTERNAL_ERROR = 6,
  BB_STATUS_INVALID_UTF8 = 7,
  BB_STATUS_NOT_FOUND = 8,
  BB_STATUS_CANCELLED = 9
} bb_status;

typedef struct bb_bytes {
  /* Borrowed bytes. data may be NULL only when length is zero. */
  const uint8_t *data;
  size_t length;
} bb_bytes;

typedef struct bb_string_view {
  /* Borrowed UTF-8 bytes; not necessarily null terminated. */
  const char *data;
  size_t length;
} bb_string_view;

typedef enum bb_log_level {
  BB_LOG_TRACE = 0,
  BB_LOG_INFO = 1,
  BB_LOG_WARNING = 2,
  BB_LOG_ERROR = 3
} bb_log_level;

/* The message is borrowed for the duration of the callback. */
typedef void (*bb_log_fn)(void *user, bb_log_level level, bb_string_view message);

typedef uint32_t bb_source_id;
typedef uint32_t bb_symbol;

#define BB_SOURCE_ID_NONE ((bb_source_id)0)
#define BB_SYMBOL_NONE ((bb_symbol)0)

typedef struct bb_span {
  bb_source_id source_id;
  uint32_t byte_start;
  uint32_t byte_end;
} bb_span;

typedef enum bb_utf8_policy {
  BB_UTF8_REJECT_INVALID = 0,
  BB_UTF8_ALLOW_INVALID = 1
} bb_utf8_policy;

typedef enum bb_diagnostic_severity {
  BB_DIAGNOSTIC_NOTE = 0,
  BB_DIAGNOSTIC_WARNING = 1,
  BB_DIAGNOSTIC_ERROR = 2
} bb_diagnostic_severity;

typedef struct bb_diagnostic {
  bb_diagnostic_severity severity;
  uint32_t code;
  /* Views returned by an owning object remain valid for that object's lifetime. */
  bb_string_view message;
  bb_span primary_span;
  const bb_span *related_spans;
  size_t related_span_count;
} bb_diagnostic;

typedef struct bb_source_info {
  bb_source_id id;
  bb_string_view name;
  bb_bytes contents;
} bb_source_info;

typedef struct bb_limits {
  /* Zero disables the corresponding resource rather than meaning unlimited. */
  size_t max_total_allocation_bytes;
  size_t max_source_bytes;
  size_t max_arena_bytes;
  uint32_t max_sources;
  uint32_t max_diagnostics;
  uint32_t max_related_spans;
  uint32_t max_interned_strings;
  uint32_t max_provenance_records;
  uint32_t max_syntax_tokens;
  uint32_t max_syntax_nodes;
  uint32_t max_syntax_depth;
  uint32_t max_collection_depth;
  uint32_t max_collection_item_values;
  uint32_t max_import_depth;
  uint32_t max_graph_depth;
  uint64_t max_graph_nodes;
  uint64_t max_collection_cardinality;
  uint64_t max_output_count;
  /* Maximum pixels in any one allocated raster surface. */
  uint64_t max_render_pixels;
} bb_limits;

typedef void *(*bb_alloc_fn)(void *user, size_t size, size_t alignment);
typedef void *(*bb_realloc_fn)(void *user, void *pointer, size_t old_size, size_t new_size, size_t alignment);
typedef void (*bb_free_fn)(void *user, void *pointer, size_t size, size_t alignment);

typedef struct bb_allocator {
  void *user;
  /* alignment is a non-zero power of two. A successful allocation owns size bytes. */
  bb_alloc_fn alloc;
  bb_realloc_fn realloc;
  bb_free_fn free;
} bb_allocator;

typedef struct bb_context_desc {
  /* Initialize with bb_context_desc_init before overriding fields. */
  uint32_t struct_size;
  bb_allocator allocator;
  bb_limits limits;
  bb_utf8_policy utf8_policy;
  void *log_user;
  bb_log_fn log;
} bb_context_desc;

BB_API void bb_limits_init(bb_limits *limits);
BB_API void bb_context_desc_init(bb_context_desc *desc);
BB_API bb_status bb_context_create(const bb_context_desc *desc, bb_context **out_context);
BB_API void bb_context_destroy(bb_context *context);
BB_API bb_status bb_context_get_limits(const bb_context *context, bb_limits *out_limits);
BB_API bb_status bb_utf8_validate(bb_bytes bytes);
/* Copies name and contents. Source IDs and returned source views live until context destruction. */
BB_API bb_status bb_context_add_source(bb_context *context, bb_string_view name, bb_bytes contents, bb_source_id *out_source_id);
BB_API size_t bb_context_source_count(const bb_context *context);
BB_API bb_status bb_context_source_info(const bb_context *context, bb_source_id source_id, bb_source_info *out_info);
BB_API bb_status bb_context_validate_span(const bb_context *context, bb_span span);
BB_API const char *bb_status_name(bb_status status);

#ifdef __cplusplus
}
#endif

#endif
