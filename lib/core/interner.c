#include "interner.h"

#include "checked_math.h"
#include "context_internal.h"

#include <string.h>

struct bb_interned_string {
  uint64_t hash;
  char *data;
  size_t length;
};

static uint64_t bb_string_hash(bb_string_view value) {
  uint64_t hash = UINT64_C(14695981039346656037);
  size_t index;
  for (index = 0; index < value.length; index += 1) {
    hash ^= (uint8_t)value.data[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

bb_status bb_interner_init(bb_interner *interner, bb_context *context) {
  if (interner == NULL || context == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  interner->context = context;
  interner->entries = NULL;
  interner->count = 0;
  interner->capacity = 0;
  return BB_STATUS_OK;
}

void bb_interner_destroy(bb_interner *interner) {
  size_t index;
  size_t entry_bytes;
  if (interner == NULL || interner->context == NULL) {
    return;
  }
  for (index = 0; index < interner->count; index += 1) {
    bb_context_deallocate(
      interner->context,
      interner->entries[index].data,
      interner->entries[index].length,
      _Alignof(char)
    );
  }
  entry_bytes = interner->capacity * sizeof(*interner->entries);
  bb_context_deallocate(interner->context, interner->entries, entry_bytes, _Alignof(bb_interned_string));
  interner->context = NULL;
  interner->entries = NULL;
  interner->count = 0;
  interner->capacity = 0;
}

static bb_status bb_interner_grow(bb_interner *interner, const bb_limits *limits) {
  size_t new_capacity;
  size_t old_size;
  size_t new_size;
  void *new_entries;
  bb_status status;
  if (interner->count < interner->capacity) {
    return BB_STATUS_OK;
  }
  new_capacity = interner->capacity == 0 ? 8 : interner->capacity * 2;
  if (new_capacity < interner->capacity) {
    return BB_STATUS_OVERFLOW;
  }
  if (new_capacity > limits->max_interned_strings) {
    new_capacity = limits->max_interned_strings;
  }
  if (new_capacity <= interner->capacity) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  if (!bb_size_multiply(interner->capacity, sizeof(*interner->entries), &old_size) ||
      !bb_size_multiply(new_capacity, sizeof(*interner->entries), &new_size)) {
    return BB_STATUS_OVERFLOW;
  }
  status = bb_context_reallocate(
    interner->context,
    interner->entries,
    old_size,
    new_size,
    _Alignof(bb_interned_string),
    &new_entries
  );
  if (status != BB_STATUS_OK) {
    return status;
  }
  interner->entries = new_entries;
  interner->capacity = new_capacity;
  return BB_STATUS_OK;
}

bb_status bb_interner_intern(bb_interner *interner, bb_string_view value, bb_symbol *out_symbol) {
  bb_limits limits;
  uint64_t hash;
  size_t index;
  char *copy = NULL;
  bb_status status;
  if (interner == NULL || interner->context == NULL || out_symbol == NULL ||
      (value.length != 0 && value.data == NULL)) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  *out_symbol = BB_SYMBOL_NONE;
  if (bb_context_utf8_policy(interner->context) == BB_UTF8_REJECT_INVALID) {
    status = bb_utf8_validate((bb_bytes){(const uint8_t *)value.data, value.length});
    if (status != BB_STATUS_OK) {
      return status;
    }
  }
  hash = bb_string_hash(value);
  for (index = 0; index < interner->count; index += 1) {
    const bb_interned_string *entry = &interner->entries[index];
    if (entry->hash == hash && entry->length == value.length &&
        (value.length == 0 || memcmp(entry->data, value.data, value.length) == 0)) {
      *out_symbol = (bb_symbol)(index + 1);
      return BB_STATUS_OK;
    }
  }
  status = bb_context_get_limits(interner->context, &limits);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (interner->count >= limits.max_interned_strings) {
    return BB_STATUS_LIMIT_EXCEEDED;
  }
  status = bb_interner_grow(interner, &limits);
  if (status != BB_STATUS_OK) {
    return status;
  }
  status = bb_context_allocate(interner->context, value.length, _Alignof(char), (void **)&copy);
  if (status != BB_STATUS_OK) {
    return status;
  }
  if (value.length != 0) {
    memcpy(copy, value.data, value.length);
  }
  interner->entries[interner->count].hash = hash;
  interner->entries[interner->count].data = copy;
  interner->entries[interner->count].length = value.length;
  interner->count += 1;
  *out_symbol = (bb_symbol)interner->count;
  return BB_STATUS_OK;
}

bb_status bb_interner_lookup(const bb_interner *interner, bb_symbol symbol, bb_string_view *out_value) {
  const bb_interned_string *entry;
  if (interner == NULL || interner->context == NULL || out_value == NULL) {
    return BB_STATUS_INVALID_ARGUMENT;
  }
  if (symbol == BB_SYMBOL_NONE || symbol > interner->count) {
    return BB_STATUS_NOT_FOUND;
  }
  entry = &interner->entries[symbol - 1];
  out_value->data = entry->data;
  out_value->length = entry->length;
  return BB_STATUS_OK;
}

size_t bb_interner_count(const bb_interner *interner) {
  return interner == NULL ? 0 : interner->count;
}
