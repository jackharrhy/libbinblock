#include "diagnostic.h"

#include "checked_math.h"
#include "context_internal.h"

#include <string.h>

struct bb_owned_diagnostic {
  bb_diagnostic value;
  uint8_t *payload;
  size_t payload_size;
};

static int bb_diagnostic_severity_is_valid(bb_diagnostic_severity severity) {
  return severity == BB_DIAGNOSTIC_NOTE || severity == BB_DIAGNOSTIC_WARNING || severity == BB_DIAGNOSTIC_ERROR;
}

bb_status bb_diagnostic_store_init(bb_diagnostic_store *store, bb_context *context) {
  if (store == NULL || context == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  store->context = context;
  store->items = NULL;
  store->count = 0;
  store->capacity = 0;
  return BB_STATUS_OK;
}

void bb_diagnostic_store_destroy(bb_diagnostic_store *store) {
  size_t index;
  size_t item_bytes;
  if (store == NULL || store->context == NULL) {
    return;
  }
  for (index = 0; index < store->count; index += 1) {
    bb_context_deallocate(
      store->context,
      store->items[index].payload,
      store->items[index].payload_size,
      _Alignof(bb_span)
    );
  }
  item_bytes = store->capacity * sizeof(*store->items);
  bb_context_deallocate(store->context, store->items, item_bytes, _Alignof(bb_owned_diagnostic));
  store->context = NULL;
  store->items = NULL;
  store->count = 0;
  store->capacity = 0;
}

static bb_status bb_diagnostic_store_grow(bb_diagnostic_store *store, const bb_limits *limits) {
  size_t new_capacity;
  size_t old_size;
  size_t new_size;
  void *new_items;
  bb_status status;
  if (store->count < store->capacity) {
    return BB_STATUS_OK;
  }
  new_capacity = store->capacity == 0 ? 4 : store->capacity * 2;
  if (new_capacity < store->capacity) {
    return BB_STATUS_OVERFLOW;
  }
  if (new_capacity > limits->max_diagnostics) {
    new_capacity = limits->max_diagnostics;
  }
  if (new_capacity <= store->capacity) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  if (!bb_size_multiply(store->capacity, sizeof(*store->items), &old_size) ||
      !bb_size_multiply(new_capacity, sizeof(*store->items), &new_size)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_reallocate(
    store->context,
    store->items,
    old_size,
    new_size,
    _Alignof(bb_owned_diagnostic),
    &new_items
  );
  if (status != BB_STATUS_OK) {
    return status;
  }
  store->items = new_items;
  store->capacity = new_capacity;
  return BB_STATUS_OK;
}

bb_status bb_diagnostic_store_push(
  bb_diagnostic_store *store,
  bb_diagnostic_severity severity,
  uint32_t code,
  bb_string_view message,
  bb_span primary_span,
  const bb_span *related_spans,
  size_t related_span_count
) {
  bb_limits limits;
  bb_owned_diagnostic *owned;
  size_t related_bytes;
  size_t payload_size;
  uint8_t *payload = NULL;
  size_t index;
  bb_status status;
  if (store == NULL || store->context == NULL || !bb_diagnostic_severity_is_valid(severity) ||
      (message.length != 0 && message.data == NULL) || (related_span_count != 0 && related_spans == NULL)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  status = bb_context_validate_span(store->context, primary_span);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (bb_context_utf8_policy(store->context) == BB_UTF8_REJECT_INVALID) {
    status = bb_utf8_validate((bb_bytes){(const uint8_t *)message.data, message.length});
    if (status != BB_STATUS_OK) {
      return status;
    }
  }
  status = bb_context_get_limits(store->context, &limits);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (store->count >= limits.max_diagnostics || related_span_count > limits.max_related_spans) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  for (index = 0; index < related_span_count; index += 1) {
    status = bb_context_validate_span(store->context, related_spans[index]);
    if (status != BB_STATUS_OK) {
      return status;
    }
  }
  if (!bb_size_multiply(related_span_count, sizeof(*related_spans), &related_bytes) ||
      !bb_size_add(related_bytes, message.length, &payload_size)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_diagnostic_store_grow(store, &limits);
  if (status != BB_STATUS_OK) {
    return status;
  }
  status = bb_context_allocate(store->context, payload_size, _Alignof(bb_span), (void **)&payload);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (related_bytes != 0) {
    memcpy(payload, related_spans, related_bytes);
  }
  if (message.length != 0) {
    memcpy(payload + related_bytes, message.data, message.length);
  }
  owned = &store->items[store->count];
  owned->payload = payload;
  owned->payload_size = payload_size;
  owned->value.severity = severity;
  owned->value.code = code;
  owned->value.message.data = message.length == 0 ? NULL : (const char *)(payload + related_bytes);
  owned->value.message.length = message.length;
  owned->value.primary_span = primary_span;
  owned->value.related_spans = related_span_count == 0 ? NULL : (const bb_span *)payload;
  owned->value.related_span_count = related_span_count;
  store->count += 1;
  return BB_STATUS_OK;
}

size_t bb_diagnostic_store_count(const bb_diagnostic_store *store) {
  return store == NULL ? 0 : store->count;
}

bb_status bb_diagnostic_store_get(const bb_diagnostic_store *store, size_t index, bb_diagnostic *out_diagnostic) {
  if (store == NULL || store->context == NULL || out_diagnostic == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  if (index >= store->count) {
    return BB_STATUS_NOT_FOUND;
  }
  *out_diagnostic = store->items[index].value;
  return BB_STATUS_OK;
}
