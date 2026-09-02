#include <binblock/collection.h>

#include "checked_math.h"
#include "context_internal.h"

#include <math.h>
#include <string.h>

typedef enum bb_collection_kind {
  BB_COLLECTION_VALUES,
  BB_COLLECTION_MAP,
  BB_COLLECTION_FILTER,
  BB_COLLECTION_FLAT_MAP,
  BB_COLLECTION_CONCAT,
  BB_COLLECTION_ZIP,
  BB_COLLECTION_PRODUCT,
  BB_COLLECTION_SLICE,
  BB_COLLECTION_SELECT_KEY
} bb_collection_kind;

struct bb_collection {
  bb_context *context;
  bb_collection_kind kind;
  uint32_t references;
  uint32_t depth;
  size_t width;
  union {
    struct {
      bb_value *values;
      size_t value_bytes;
      uint8_t *strings;
      size_t string_bytes;
      uint64_t count;
    } values;
    struct {
      bb_collection *source;
      bb_collection_map_fn callback;
      void *user;
    } map;
    struct {
      bb_collection *source;
      bb_collection_predicate_fn callback;
      void *user;
    } filter;
    struct {
      bb_collection *source;
      bb_collection_flat_count_fn count;
      bb_collection_flat_get_fn get;
      void *user;
    } flat_map;
    struct {
      bb_collection *left;
      bb_collection *right;
    } pair;
    struct {
      bb_collection *source;
      uint64_t start;
      uint64_t count;
    } slice;
    struct {
      bb_collection *source;
      char *key;
      size_t key_length;
    } select_key;
  } data;
};

static int bb_value_kind_is_valid(bb_value_kind kind) {
  return kind >= BB_VALUE_BOOL && kind <= BB_VALUE_CALLABLE;
}

static bb_status bb_collection_string_validate(const bb_context *context, bb_string_view value) {
  if (value.length != 0 && value.data == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (bb_context_utf8_policy(context) == BB_UTF8_ALLOW_INVALID) return BB_STATUS_OK;
  return bb_utf8_validate((bb_bytes){(const uint8_t *)value.data, value.length});
}

static bb_status bb_value_validate(const bb_context *context, const bb_value *value) {
  bb_status status;
  if (value == NULL || !bb_value_kind_is_valid(value->kind)) return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_BOOL && value->data.boolean > 1) return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_NUMBER && !isfinite(value->data.number)) return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_DEGREES && !isfinite(value->data.degrees)) return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_PERCENTAGE && !isfinite(value->data.percentage)) return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_VECTOR2 && (!isfinite(value->data.vector2.x) || !isfinite(value->data.vector2.y)))
    return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_STRING) return bb_collection_string_validate(context, value->data.string);
  if (value->kind == BB_VALUE_ARTIFACT) {
    const bb_artifact_value *artifact = &value->data.artifact;
    if (artifact->key.length == 0 || artifact->alias_identity < BB_ALIAS_NONE ||
        artifact->alias_identity > BB_ALIAS_BYTES ||
        (artifact->alias_identity == BB_ALIAS_NONE && artifact->alias_target.length != 0) ||
        (artifact->alias_identity != BB_ALIAS_NONE && artifact->alias_target.length == 0)) return BB_STATUS_INVALID_ARGUMENT;
    status = bb_collection_string_validate(context, artifact->key);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_string_validate(context, artifact->path);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_string_validate(context, artifact->alias_target);
    if (status != BB_STATUS_OK) return status;
    status = bb_context_validate_span(context, artifact->provenance);
    if (status != BB_STATUS_OK) return status;
  }
  if (value->kind == BB_VALUE_CALLABLE && value->data.callable == 0) return BB_STATUS_INVALID_ARGUMENT;
  if (value->kind == BB_VALUE_ASSET) {
    const bb_asset_reference *asset = &value->data.asset;
    if (asset->logical_id.length == 0 || asset->has_dimensions > 1 ||
        (asset->has_dimensions && (asset->width == 0 || asset->height == 0))) return BB_STATUS_INVALID_ARGUMENT;
    status = bb_collection_string_validate(context, asset->logical_id);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_string_validate(context, asset->content_hash);
    if (status != BB_STATUS_OK) return status;
  }
  return BB_STATUS_OK;
}

static bb_status bb_value_string_bytes(const bb_value *value, size_t *out_bytes) {
  size_t bytes;
  *out_bytes = 0;
  if (value->kind == BB_VALUE_STRING) {
    *out_bytes = value->data.string.length;
    return BB_STATUS_OK;
  }
  if (value->kind == BB_VALUE_ASSET) {
    if (!bb_size_add(value->data.asset.logical_id.length, value->data.asset.content_hash.length, out_bytes))
      return BB_STATUS_OVERFLOW;
    return BB_STATUS_OK;
  }
  if (value->kind != BB_VALUE_ARTIFACT) return BB_STATUS_OK;
  if (!bb_size_add(value->data.artifact.key.length, value->data.artifact.path.length, &bytes) ||
      !bb_size_add(bytes, value->data.artifact.alias_target.length, out_bytes)) return BB_STATUS_OVERFLOW;
  return BB_STATUS_OK;
}

static bb_status bb_values_validate(const bb_context *context, const bb_value *values, size_t count) {
  size_t index;
  for (index = 0; index < count; index += 1) {
    bb_status status = bb_value_validate(context, &values[index]);
    if (status != BB_STATUS_OK) return status;
  }
  return BB_STATUS_OK;
}

static char *bb_copy_string(uint8_t *storage, size_t *offset, bb_string_view source) {
  char *copy;
  if (source.length == 0) return NULL;
  copy = (char *)(storage + *offset);
  memcpy(copy, source.data, source.length);
  *offset += source.length;
  return copy;
}

static bb_status bb_collection_allocate(
  bb_context *context,
  bb_collection_kind kind,
  uint32_t depth,
  size_t width,
  bb_collection **out_collection
) {
  bb_limits limits;
  bb_collection *collection;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (context == NULL || width == 0) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_context_get_limits(context, &limits);
  if (status != BB_STATUS_OK) return status;
  if (depth > limits.max_collection_depth || width > limits.max_collection_item_values) return BB_STATUS_LIMIT_EXCEEDED;
  status = bb_context_allocate(context, sizeof(*collection), _Alignof(bb_collection), (void **)&collection);
  if (status != BB_STATUS_OK) return status;
  memset(collection, 0, sizeof(*collection));
  collection->context = context;
  collection->kind = kind;
  collection->references = 1;
  collection->depth = depth;
  collection->width = width;
  *out_collection = collection;
  return BB_STATUS_OK;
}

bb_status bb_collection_from_rows(
  bb_context *context,
  const bb_value *values,
  size_t item_count,
  size_t item_width,
  bb_collection **out_collection
) {
  bb_collection *collection;
  size_t value_count;
  size_t value_bytes;
  size_t string_bytes = 0;
  size_t string_offset = 0;
  size_t index;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (item_width == 0 || !bb_size_multiply(item_count, item_width, &value_count))
    return item_width == 0 ? BB_STATUS_INVALID_ARGUMENT : BB_STATUS_OVERFLOW;
  if (value_count != 0 && values == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (!bb_size_multiply(value_count, sizeof(*values), &value_bytes)) return BB_STATUS_OVERFLOW;
  for (index = 0; index < value_count; index += 1) {
    size_t item_bytes;
    status = bb_value_validate(context, &values[index]);
    if (status != BB_STATUS_OK) return status;
    status = bb_value_string_bytes(&values[index], &item_bytes);
    if (status != BB_STATUS_OK) return status;
    if (!bb_size_add(string_bytes, item_bytes, &string_bytes)) return BB_STATUS_OVERFLOW;
  }
  status = bb_collection_allocate(context, BB_COLLECTION_VALUES, 1, item_width, &collection);
  if (status != BB_STATUS_OK) return status;
  collection->data.values.value_bytes = value_bytes;
  collection->data.values.string_bytes = string_bytes;
  status = bb_context_allocate(context, value_bytes, _Alignof(bb_value), (void **)&collection->data.values.values);
  if (status != BB_STATUS_OK) goto fail;
  status = bb_context_allocate(context, string_bytes, _Alignof(char), (void **)&collection->data.values.strings);
  if (status != BB_STATUS_OK) goto fail;
  if (value_bytes != 0) memcpy(collection->data.values.values, values, value_bytes);
  for (index = 0; index < value_count; index += 1) {
    bb_value *copy = &collection->data.values.values[index];
    if (copy->kind == BB_VALUE_STRING) {
      copy->data.string.data = bb_copy_string(collection->data.values.strings, &string_offset, values[index].data.string);
    } else if (copy->kind == BB_VALUE_ARTIFACT) {
      copy->data.artifact.key.data =
        bb_copy_string(collection->data.values.strings, &string_offset, values[index].data.artifact.key);
      copy->data.artifact.path.data =
        bb_copy_string(collection->data.values.strings, &string_offset, values[index].data.artifact.path);
      copy->data.artifact.alias_target.data =
        bb_copy_string(collection->data.values.strings, &string_offset, values[index].data.artifact.alias_target);
    } else if (copy->kind == BB_VALUE_ASSET) {
      copy->data.asset.logical_id.data =
        bb_copy_string(collection->data.values.strings, &string_offset, values[index].data.asset.logical_id);
      copy->data.asset.content_hash.data =
        bb_copy_string(collection->data.values.strings, &string_offset, values[index].data.asset.content_hash);
    }
  }
  collection->data.values.count = item_count;
  *out_collection = collection;
  return BB_STATUS_OK;

fail:
  bb_collection_release(collection);
  return status;
}

bb_status bb_collection_from_values(
  bb_context *context,
  const bb_value *values,
  size_t value_count,
  bb_collection **out_collection
) {
  return bb_collection_from_rows(context, values, value_count, 1, out_collection);
}

bb_collection *bb_collection_retain(bb_collection *collection) {
  if (collection == NULL || collection->references == UINT32_MAX) return NULL;
  collection->references += 1;
  return collection;
}

void bb_collection_release(bb_collection *collection) {
  bb_context *context;
  if (collection == NULL) return;
  collection->references -= 1;
  if (collection->references != 0) return;
  context = collection->context;
  if (collection->kind == BB_COLLECTION_VALUES) {
    bb_context_deallocate(
      context,
      collection->data.values.values,
      collection->data.values.value_bytes,
      _Alignof(bb_value)
    );
    bb_context_deallocate(
      context,
      collection->data.values.strings,
      collection->data.values.string_bytes,
      _Alignof(char)
    );
  } else if (collection->kind == BB_COLLECTION_MAP) {
    bb_collection_release(collection->data.map.source);
  } else if (collection->kind == BB_COLLECTION_FILTER) {
    bb_collection_release(collection->data.filter.source);
  } else if (collection->kind == BB_COLLECTION_FLAT_MAP) {
    bb_collection_release(collection->data.flat_map.source);
  } else if (collection->kind == BB_COLLECTION_CONCAT || collection->kind == BB_COLLECTION_ZIP ||
             collection->kind == BB_COLLECTION_PRODUCT) {
    bb_collection_release(collection->data.pair.left);
    bb_collection_release(collection->data.pair.right);
  } else if (collection->kind == BB_COLLECTION_SLICE) {
    bb_collection_release(collection->data.slice.source);
  } else if (collection->kind == BB_COLLECTION_SELECT_KEY) {
    bb_collection_release(collection->data.select_key.source);
    bb_context_deallocate(
      context,
      collection->data.select_key.key,
      collection->data.select_key.key_length,
      _Alignof(char)
    );
  }
  bb_context_deallocate(context, collection, sizeof(*collection), _Alignof(bb_collection));
}

static bb_status bb_collection_create_unary(
  bb_collection *source,
  bb_collection_kind kind,
  size_t width,
  bb_collection **out_collection
) {
  if (source == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (source->depth == UINT32_MAX) return BB_STATUS_OVERFLOW;
  return bb_collection_allocate(source->context, kind, source->depth + 1, width, out_collection);
}

static bb_status bb_collection_retain_source(bb_collection *collection, bb_collection *source) {
  bb_collection *retained = bb_collection_retain(source);
  if (retained == NULL) {
    bb_collection_release(collection);
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  if (collection->kind == BB_COLLECTION_MAP) collection->data.map.source = retained;
  else if (collection->kind == BB_COLLECTION_FILTER) collection->data.filter.source = retained;
  else if (collection->kind == BB_COLLECTION_FLAT_MAP) collection->data.flat_map.source = retained;
  else if (collection->kind == BB_COLLECTION_SLICE) collection->data.slice.source = retained;
  else collection->data.select_key.source = retained;
  return BB_STATUS_OK;
}

bb_status bb_collection_map(
  bb_collection *source,
  size_t output_width,
  bb_collection_map_fn map,
  void *user,
  bb_collection **out_collection
) {
  bb_collection *collection;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (map == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_collection_create_unary(source, BB_COLLECTION_MAP, output_width, &collection);
  if (status != BB_STATUS_OK) return status;
  status = bb_collection_retain_source(collection, source);
  if (status != BB_STATUS_OK) return status;
  collection->data.map.callback = map;
  collection->data.map.user = user;
  *out_collection = collection;
  return BB_STATUS_OK;
}

bb_status bb_collection_filter(
  bb_collection *source,
  bb_collection_predicate_fn predicate,
  void *user,
  bb_collection **out_collection
) {
  bb_collection *collection;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (predicate == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_collection_create_unary(source, BB_COLLECTION_FILTER, source == NULL ? 0 : source->width, &collection);
  if (status != BB_STATUS_OK) return status;
  status = bb_collection_retain_source(collection, source);
  if (status != BB_STATUS_OK) return status;
  collection->data.filter.callback = predicate;
  collection->data.filter.user = user;
  *out_collection = collection;
  return BB_STATUS_OK;
}

bb_status bb_collection_flat_map(
  bb_collection *source,
  size_t output_width,
  bb_collection_flat_count_fn count,
  bb_collection_flat_get_fn get,
  void *user,
  bb_collection **out_collection
) {
  bb_collection *collection;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (count == NULL || get == NULL) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_collection_create_unary(source, BB_COLLECTION_FLAT_MAP, output_width, &collection);
  if (status != BB_STATUS_OK) return status;
  status = bb_collection_retain_source(collection, source);
  if (status != BB_STATUS_OK) return status;
  collection->data.flat_map.count = count;
  collection->data.flat_map.get = get;
  collection->data.flat_map.user = user;
  *out_collection = collection;
  return BB_STATUS_OK;
}

static bb_status bb_collection_create_pair(
  bb_collection *left,
  bb_collection *right,
  bb_collection_kind kind,
  size_t width,
  bb_collection **out_collection
) {
  bb_collection *collection;
  uint32_t depth;
  bb_status status;
  bb_collection *retained_left;
  bb_collection *retained_right;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (left == NULL || right == NULL || left->context != right->context) return BB_STATUS_INVALID_ARGUMENT;
  if (left->depth == UINT32_MAX || right->depth == UINT32_MAX) return BB_STATUS_OVERFLOW;
  depth = (left->depth > right->depth ? left->depth : right->depth) + 1;
  status = bb_collection_allocate(left->context, kind, depth, width, &collection);
  if (status != BB_STATUS_OK) return status;
  retained_left = bb_collection_retain(left);
  retained_right = bb_collection_retain(right);
  if (retained_left == NULL || retained_right == NULL) {
    if (retained_left != NULL) bb_collection_release(retained_left);
    if (retained_right != NULL) bb_collection_release(retained_right);
    bb_collection_release(collection);
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  collection->data.pair.left = retained_left;
  collection->data.pair.right = retained_right;
  *out_collection = collection;
  return BB_STATUS_OK;
}

bb_status bb_collection_concat(bb_collection *left, bb_collection *right, bb_collection **out_collection) {
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (left == NULL || right == NULL || left->width != right->width) return BB_STATUS_INVALID_ARGUMENT;
  return bb_collection_create_pair(left, right, BB_COLLECTION_CONCAT, left->width, out_collection);
}

bb_status bb_collection_zip(bb_collection *left, bb_collection *right, bb_collection **out_collection) {
  size_t width;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (left == NULL || right == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (!bb_size_add(left->width, right->width, &width)) return BB_STATUS_OVERFLOW;
  return bb_collection_create_pair(left, right, BB_COLLECTION_ZIP, width, out_collection);
}

bb_status bb_collection_product(bb_collection *left, bb_collection *right, bb_collection **out_collection) {
  size_t width;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (left == NULL || right == NULL) return BB_STATUS_INVALID_ARGUMENT;
  if (!bb_size_add(left->width, right->width, &width)) return BB_STATUS_OVERFLOW;
  return bb_collection_create_pair(left, right, BB_COLLECTION_PRODUCT, width, out_collection);
}

bb_status bb_collection_slice(
  bb_collection *source,
  uint64_t start,
  uint64_t count,
  bb_collection **out_collection
) {
  bb_collection *collection;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  status = bb_collection_create_unary(source, BB_COLLECTION_SLICE, source == NULL ? 0 : source->width, &collection);
  if (status != BB_STATUS_OK) return status;
  status = bb_collection_retain_source(collection, source);
  if (status != BB_STATUS_OK) return status;
  collection->data.slice.start = start;
  collection->data.slice.count = count;
  *out_collection = collection;
  return BB_STATUS_OK;
}

bb_status bb_collection_select_key(
  bb_collection *source,
  bb_string_view key,
  bb_collection **out_collection
) {
  bb_collection *collection;
  bb_status status;
  if (out_collection == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_collection = NULL;
  if (source == NULL || source->width != 1 || (key.length != 0 && key.data == NULL)) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_collection_create_unary(source, BB_COLLECTION_SELECT_KEY, 1, &collection);
  if (status != BB_STATUS_OK) return status;
  collection->data.select_key.key_length = key.length;
  status = bb_context_allocate(source->context, key.length, _Alignof(char), (void **)&collection->data.select_key.key);
  if (status != BB_STATUS_OK) {
    bb_collection_release(collection);
    return status;
  }
  if (key.length != 0) memcpy(collection->data.select_key.key, key.data, key.length);
  status = bb_collection_retain_source(collection, source);
  if (status != BB_STATUS_OK) return status;
  *out_collection = collection;
  return BB_STATUS_OK;
}

static bb_status bb_collection_scratch(const bb_collection *collection, bb_value **out_values, size_t *out_bytes) {
  size_t bytes;
  if (!bb_size_multiply(collection->width, sizeof(bb_value), &bytes)) return BB_STATUS_OVERFLOW;
  *out_bytes = bytes;
  return bb_context_allocate(collection->context, bytes, _Alignof(bb_value), (void **)out_values);
}

static bb_status bb_collection_limit_count(const bb_collection *collection, uint64_t count, uint64_t *out_count) {
  bb_limits limits;
  bb_status status = bb_context_get_limits(collection->context, &limits);
  if (status != BB_STATUS_OK) return status;
  if (count > limits.max_collection_cardinality) return BB_STATUS_LIMIT_EXCEEDED;
  *out_count = count;
  return BB_STATUS_OK;
}

bb_status bb_collection_count(const bb_collection *collection, uint64_t *out_count) {
  uint64_t left_count;
  uint64_t right_count;
  bb_status status;
  if (collection == NULL || out_count == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_count = 0;
  if (collection->kind == BB_COLLECTION_VALUES)
    return bb_collection_limit_count(collection, collection->data.values.count, out_count);
  if (collection->kind == BB_COLLECTION_MAP) {
    status = bb_collection_count(collection->data.map.source, &left_count);
    return status == BB_STATUS_OK ? bb_collection_limit_count(collection, left_count, out_count) : status;
  }
  if (collection->kind == BB_COLLECTION_CONCAT || collection->kind == BB_COLLECTION_ZIP ||
      collection->kind == BB_COLLECTION_PRODUCT) {
    status = bb_collection_count(collection->data.pair.left, &left_count);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_count(collection->data.pair.right, &right_count);
    if (status != BB_STATUS_OK) return status;
    if (collection->kind == BB_COLLECTION_ZIP) {
      if (left_count != right_count) return BB_STATUS_INVALID_ARGUMENT;
      return bb_collection_limit_count(collection, left_count, out_count);
    }
    if (collection->kind == BB_COLLECTION_CONCAT) {
      if (UINT64_MAX - left_count < right_count) return BB_STATUS_LIMIT_EXCEEDED;
      return bb_collection_limit_count(collection, left_count + right_count, out_count);
    }
    if (right_count != 0 && left_count > UINT64_MAX / right_count) return BB_STATUS_LIMIT_EXCEEDED;
    return bb_collection_limit_count(collection, left_count * right_count, out_count);
  }
  if (collection->kind == BB_COLLECTION_SLICE) {
    status = bb_collection_count(collection->data.slice.source, &left_count);
    if (status != BB_STATUS_OK) return status;
    if (collection->data.slice.start >= left_count) return bb_collection_limit_count(collection, 0, out_count);
    left_count -= collection->data.slice.start;
    if (left_count > collection->data.slice.count) left_count = collection->data.slice.count;
    return bb_collection_limit_count(collection, left_count, out_count);
  }
  {
    const bb_collection *source = collection->kind == BB_COLLECTION_FILTER
                                    ? collection->data.filter.source
                                    : collection->kind == BB_COLLECTION_FLAT_MAP ? collection->data.flat_map.source
                                                                                : collection->data.select_key.source;
    bb_value *scratch = NULL;
    size_t scratch_bytes = 0;
    uint64_t source_count;
    uint64_t count = 0;
    uint64_t index;
    status = bb_collection_count(source, &source_count);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_scratch(source, &scratch, &scratch_bytes);
    if (status != BB_STATUS_OK) return status;
    for (index = 0; index < source_count; index += 1) {
      status = bb_collection_get(source, index, scratch, source->width);
      if (status != BB_STATUS_OK) break;
      if (collection->kind == BB_COLLECTION_FILTER) {
        uint32_t matches = 0;
        status = collection->data.filter.callback(collection->data.filter.user, scratch, source->width, &matches);
        if (status != BB_STATUS_OK) break;
        if (matches) count += 1;
      } else if (collection->kind == BB_COLLECTION_FLAT_MAP) {
        uint64_t nested_count;
        status = collection->data.flat_map.count(collection->data.flat_map.user, scratch, source->width, &nested_count);
        if (status != BB_STATUS_OK) break;
        if (UINT64_MAX - count < nested_count) {
          status = BB_STATUS_LIMIT_EXCEEDED;
          break;
        }
        count += nested_count;
      } else if (scratch[0].kind == BB_VALUE_ARTIFACT &&
                 scratch[0].data.artifact.key.length == collection->data.select_key.key_length &&
                 (collection->data.select_key.key_length == 0 ||
                  memcmp(scratch[0].data.artifact.key.data, collection->data.select_key.key, collection->data.select_key.key_length) == 0)) {
        count += 1;
      }
    }
    bb_context_deallocate(collection->context, scratch, scratch_bytes, _Alignof(bb_value));
    if (status != BB_STATUS_OK) return status;
    return bb_collection_limit_count(collection, count, out_count);
  }
}

size_t bb_collection_item_width(const bb_collection *collection) {
  return collection == NULL ? 0 : collection->width;
}

bb_status bb_collection_get(
  const bb_collection *collection,
  uint64_t index,
  bb_value *out_values,
  size_t value_capacity
) {
  uint64_t count;
  bb_status status;
  if (collection == NULL || out_values == NULL || value_capacity < collection->width) return BB_STATUS_INVALID_ARGUMENT;
  status = bb_collection_count(collection, &count);
  if (status != BB_STATUS_OK) return status;
  if (index >= count) return BB_STATUS_NOT_FOUND;
  if (collection->kind == BB_COLLECTION_VALUES) {
    const size_t offset = (size_t)index * collection->width;
    memcpy(out_values, collection->data.values.values + offset, collection->width * sizeof(*out_values));
    return BB_STATUS_OK;
  }
  if (collection->kind == BB_COLLECTION_MAP) {
    bb_value *scratch;
    size_t scratch_bytes;
    status = bb_collection_scratch(collection->data.map.source, &scratch, &scratch_bytes);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_get(collection->data.map.source, index, scratch, collection->data.map.source->width);
    if (status == BB_STATUS_OK)
      status = collection->data.map.callback(
        collection->data.map.user,
        scratch,
        collection->data.map.source->width,
        out_values,
        collection->width
      );
    if (status == BB_STATUS_OK) status = bb_values_validate(collection->context, out_values, collection->width);
    bb_context_deallocate(collection->context, scratch, scratch_bytes, _Alignof(bb_value));
    return status;
  }
  if (collection->kind == BB_COLLECTION_CONCAT) {
    uint64_t left_count;
    status = bb_collection_count(collection->data.pair.left, &left_count);
    if (status != BB_STATUS_OK) return status;
    return index < left_count ? bb_collection_get(collection->data.pair.left, index, out_values, value_capacity)
                              : bb_collection_get(collection->data.pair.right, index - left_count, out_values, value_capacity);
  }
  if (collection->kind == BB_COLLECTION_ZIP || collection->kind == BB_COLLECTION_PRODUCT) {
    uint64_t right_count;
    uint64_t left_index = index;
    uint64_t right_index = index;
    status = bb_collection_count(collection->data.pair.right, &right_count);
    if (status != BB_STATUS_OK) return status;
    if (collection->kind == BB_COLLECTION_PRODUCT) {
      left_index = right_count == 0 ? 0 : index / right_count;
      right_index = right_count == 0 ? 0 : index % right_count;
    }
    status = bb_collection_get(collection->data.pair.left, left_index, out_values, collection->data.pair.left->width);
    if (status != BB_STATUS_OK) return status;
    return bb_collection_get(
      collection->data.pair.right,
      right_index,
      out_values + collection->data.pair.left->width,
      value_capacity - collection->data.pair.left->width
    );
  }
  if (collection->kind == BB_COLLECTION_SLICE)
    return bb_collection_get(collection->data.slice.source, collection->data.slice.start + index, out_values, value_capacity);
  {
    const bb_collection *source = collection->kind == BB_COLLECTION_FILTER
                                    ? collection->data.filter.source
                                    : collection->kind == BB_COLLECTION_FLAT_MAP ? collection->data.flat_map.source
                                                                                : collection->data.select_key.source;
    bb_value *scratch;
    size_t scratch_bytes;
    uint64_t source_count;
    uint64_t source_index;
    uint64_t matched = 0;
    status = bb_collection_count(source, &source_count);
    if (status != BB_STATUS_OK) return status;
    status = bb_collection_scratch(source, &scratch, &scratch_bytes);
    if (status != BB_STATUS_OK) return status;
    for (source_index = 0; source_index < source_count; source_index += 1) {
      uint64_t nested_count = 0;
      uint32_t include = 0;
      status = bb_collection_get(source, source_index, scratch, source->width);
      if (status != BB_STATUS_OK) break;
      if (collection->kind == BB_COLLECTION_FILTER) {
        status = collection->data.filter.callback(collection->data.filter.user, scratch, source->width, &include);
        nested_count = include ? 1 : 0;
      } else if (collection->kind == BB_COLLECTION_FLAT_MAP) {
        status = collection->data.flat_map.count(collection->data.flat_map.user, scratch, source->width, &nested_count);
      } else {
        include = scratch[0].kind == BB_VALUE_ARTIFACT &&
                  scratch[0].data.artifact.key.length == collection->data.select_key.key_length &&
                  (collection->data.select_key.key_length == 0 ||
                   memcmp(scratch[0].data.artifact.key.data, collection->data.select_key.key, collection->data.select_key.key_length) == 0);
        nested_count = include ? 1 : 0;
      }
      if (status != BB_STATUS_OK) break;
      if (index >= matched && index - matched < nested_count) {
        if (collection->kind == BB_COLLECTION_FLAT_MAP)
          status = collection->data.flat_map.get(
            collection->data.flat_map.user,
            scratch,
            source->width,
            index - matched,
            out_values,
            collection->width
          );
        else memcpy(out_values, scratch, source->width * sizeof(*out_values));
        if (status == BB_STATUS_OK)
          status = bb_values_validate(collection->context, out_values, collection->width);
        break;
      }
      if (UINT64_MAX - matched < nested_count) {
        status = BB_STATUS_LIMIT_EXCEEDED;
        break;
      }
      matched += nested_count;
    }
    bb_context_deallocate(collection->context, scratch, scratch_bytes, _Alignof(bb_value));
    return status;
  }
}

bb_status bb_collection_render_artifact_with(
  bb_context *context,
  const bb_collection *collection,
  uint64_t index,
  const bb_image_graph *graph,
  bb_artifact_render_fn renderer,
  void *user,
  bb_surface **out_surface
) {
  bb_value value;
  bb_limits limits;
  bb_status status;
  if (out_surface == NULL) return BB_STATUS_INVALID_ARGUMENT;
  *out_surface = NULL;
  if (context == NULL || collection == NULL || collection->context != context || collection->width != 1 ||
      graph == NULL || renderer == NULL)
    return BB_STATUS_INVALID_ARGUMENT;
  status = bb_context_get_limits(context, &limits);
  if (status != BB_STATUS_OK) return status;
  if (index >= limits.max_output_count) return BB_STATUS_LIMIT_EXCEEDED;
  status = bb_collection_get(collection, index, &value, 1);
  if (status != BB_STATUS_OK) return status;
  if (value.kind != BB_VALUE_ARTIFACT || value.data.artifact.image == BB_IMAGE_NODE_NONE) return BB_STATUS_INVALID_ARGUMENT;
  return renderer(user, context, graph, &value.data.artifact, out_surface);
}

static bb_status bb_collection_raster_renderer(
  void *user,
  bb_context *context,
  const bb_image_graph *graph,
  const bb_artifact_value *artifact,
  bb_surface **out_surface
) {
  (void)user;
  return bb_image_graph_render_raster(context, graph, artifact->image, out_surface);
}

bb_status bb_collection_render_artifact(
  bb_context *context,
  const bb_collection *collection,
  uint64_t index,
  const bb_image_graph *graph,
  bb_surface **out_surface
) {
  return bb_collection_render_artifact_with(
    context,
    collection,
    index,
    graph,
    bb_collection_raster_renderer,
    NULL,
    out_surface
  );
}
