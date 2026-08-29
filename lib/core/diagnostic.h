#ifndef BINBLOCK_DIAGNOSTIC_H
#define BINBLOCK_DIAGNOSTIC_H

#include <binblock/binblock.h>

typedef struct bb_owned_diagnostic bb_owned_diagnostic;

typedef struct bb_diagnostic_store {
  bb_context *context;
  bb_owned_diagnostic *items;
  size_t count;
  size_t capacity;
} bb_diagnostic_store;

bb_status bb_diagnostic_store_init(bb_diagnostic_store *store, bb_context *context);
void bb_diagnostic_store_destroy(bb_diagnostic_store *store);
bb_status bb_diagnostic_store_push(
  bb_diagnostic_store *store,
  bb_diagnostic_severity severity,
  uint32_t code,
  bb_string_view message,
  bb_span primary_span,
  const bb_span *related_spans,
  size_t related_span_count
);
size_t bb_diagnostic_store_count(const bb_diagnostic_store *store);
bb_status bb_diagnostic_store_get(const bb_diagnostic_store *store, size_t index, bb_diagnostic *out_diagnostic);

#endif
