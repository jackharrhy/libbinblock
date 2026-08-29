#ifndef BINBLOCK_COLLECTION_H
#define BINBLOCK_COLLECTION_H

#include <binblock/graph.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bb_collection bb_collection;

typedef enum bb_value_kind {
  BB_VALUE_BOOL = 1,
  BB_VALUE_INTEGER = 2,
  BB_VALUE_NUMBER = 3,
  BB_VALUE_STRING = 4,
  BB_VALUE_COLOR = 5,
  BB_VALUE_VECTOR2 = 6,
  BB_VALUE_IMAGE = 7,
  BB_VALUE_ARTIFACT = 8,
  BB_VALUE_DEGREES = 9,
  BB_VALUE_PERCENTAGE = 10,
  BB_VALUE_SYMBOL = 11,
  BB_VALUE_ASSET = 12,
  BB_VALUE_CALLABLE = 13
} bb_value_kind;

typedef enum bb_alias_identity {
  BB_ALIAS_NONE = 0,
  BB_ALIAS_RECIPE = 1,
  BB_ALIAS_PIXELS = 2,
  BB_ALIAS_BYTES = 3
} bb_alias_identity;

typedef struct bb_artifact_value {
  bb_string_view key;
  bb_string_view path;
  bb_image_node image;
  bb_alias_identity alias_identity;
  bb_string_view alias_target;
  bb_span provenance;
} bb_artifact_value;

typedef struct bb_asset_reference {
  bb_string_view logical_id;
  bb_string_view content_hash;
  uint32_t width;
  uint32_t height;
  uint32_t has_dimensions;
} bb_asset_reference;

typedef struct bb_value {
  bb_value_kind kind;
  union {
    uint32_t boolean;
    int64_t integer;
    double number;
    bb_string_view string;
    bb_rgba8 color;
    struct {
      double x;
      double y;
    } vector2;
    bb_image_node image;
    bb_artifact_value artifact;
    double degrees;
    double percentage;
    bb_symbol symbol;
    bb_asset_reference asset;
    uint32_t callable;
  } data;
} bb_value;

/* Callbacks are retained by derived plans; user data must outlive the collection. */
typedef bb_status (*bb_collection_map_fn)(
  void *user,
  const bb_value *input,
  size_t input_count,
  bb_value *output,
  size_t output_count
);
typedef bb_status (*bb_collection_predicate_fn)(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint32_t *out_matches
);
typedef bb_status (*bb_collection_flat_count_fn)(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint64_t *out_count
);
typedef bb_status (*bb_collection_flat_get_fn)(
  void *user,
  const bb_value *input,
  size_t input_count,
  uint64_t nested_index,
  bb_value *output,
  size_t output_count
);
typedef bb_status (*bb_artifact_render_fn)(
  void *user,
  bb_context *context,
  const bb_image_graph *graph,
  const bb_artifact_value *artifact,
  bb_surface **out_surface
);

BB_API bb_status bb_collection_from_values(
  bb_context *context,
  const bb_value *values,
  size_t value_count,
  bb_collection **out_collection
);
/* Creates item_count fixed-width rows from item_count * item_width values. */
BB_API bb_status bb_collection_from_rows(
  bb_context *context,
  const bb_value *values,
  size_t item_count,
  size_t item_width,
  bb_collection **out_collection
);
BB_API bb_collection *bb_collection_retain(bb_collection *collection);
BB_API void bb_collection_release(bb_collection *collection);
BB_API bb_status bb_collection_map(
  bb_collection *source,
  size_t output_width,
  bb_collection_map_fn map,
  void *user,
  bb_collection **out_collection
);
BB_API bb_status bb_collection_filter(
  bb_collection *source,
  bb_collection_predicate_fn predicate,
  void *user,
  bb_collection **out_collection
);
BB_API bb_status bb_collection_flat_map(
  bb_collection *source,
  size_t output_width,
  bb_collection_flat_count_fn count,
  bb_collection_flat_get_fn get,
  void *user,
  bb_collection **out_collection
);
BB_API bb_status bb_collection_concat(
  bb_collection *left,
  bb_collection *right,
  bb_collection **out_collection
);
BB_API bb_status bb_collection_zip(bb_collection *left, bb_collection *right, bb_collection **out_collection);
/* Product order is left-major: left[0]×all right items, then left[1]×all right items. */
BB_API bb_status bb_collection_product(bb_collection *left, bb_collection *right, bb_collection **out_collection);
BB_API bb_status bb_collection_slice(
  bb_collection *source,
  uint64_t start,
  uint64_t count,
  bb_collection **out_collection
);
BB_API bb_status bb_collection_select_key(
  bb_collection *source,
  bb_string_view key,
  bb_collection **out_collection
);
BB_API bb_status bb_collection_count(const bb_collection *collection, uint64_t *out_count);
BB_API size_t bb_collection_item_width(const bb_collection *collection);
/* Output string views remain valid according to their base storage or callback contract. */
BB_API bb_status bb_collection_get(
  const bb_collection *collection,
  uint64_t index,
  bb_value *out_values,
  size_t value_capacity
);
BB_API bb_status bb_collection_render_artifact(
  bb_context *context,
  const bb_collection *collection,
  uint64_t index,
  const bb_image_graph *graph,
  bb_surface **out_surface
);
/* A host renderer may implement bounded caching before delegating to a backend. */
BB_API bb_status bb_collection_render_artifact_with(
  bb_context *context,
  const bb_collection *collection,
  uint64_t index,
  const bb_image_graph *graph,
  bb_artifact_render_fn renderer,
  void *user,
  bb_surface **out_surface
);

#ifdef __cplusplus
}
#endif

#endif
