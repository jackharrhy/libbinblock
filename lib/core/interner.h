#ifndef BINBLOCK_INTERNER_H
#define BINBLOCK_INTERNER_H

#include <binblock/binblock.h>

typedef struct bb_interned_string bb_interned_string;

typedef struct bb_interner {
  bb_context *context;
  bb_interned_string *entries;
  size_t count;
  size_t capacity;
} bb_interner;

bb_status bb_interner_init(bb_interner *interner, bb_context *context);
void bb_interner_destroy(bb_interner *interner);
bb_status bb_interner_intern(bb_interner *interner, bb_string_view value, bb_symbol *out_symbol);
bb_status bb_interner_lookup(const bb_interner *interner, bb_symbol symbol, bb_string_view *out_value);
size_t bb_interner_count(const bb_interner *interner);

#endif
